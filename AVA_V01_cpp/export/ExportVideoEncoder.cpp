#include "ExportVideoEncoder.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace {

// libx264 CRF 23 is the software default. VideoToolbox has no CRF; FFmpeg's
// documented constant-quality example is -q:v 65 (1–100, higher is better).
constexpr int kVideoToolboxQuality = 65;

struct EncoderWaiter {
    QPointer<QObject> context;
    QString ffmpegPath;
    std::function<void(bool)> callback;
};

struct DeliveryBatch {
    bool available = false;
    std::vector<EncoderWaiter> waiters;
};

class EncoderProbeDispatcher;

struct EncoderAvailabilityState {
    std::mutex mutex;
    bool hasResult = false;
    bool h264VideoToolboxAvailable = false;
    QString cachedFfmpegPath;
    bool probeInFlight = false;
    QString probeFfmpegPath;
    int probeGeneration = 0;
    std::vector<EncoderWaiter> waiters;
    EncoderProbeDispatcher* dispatcher = nullptr;
};

EncoderAvailabilityState& availabilityState() {
    static EncoderAvailabilityState state;
    return state;
}

std::vector<EncoderWaiter> takeWaitersForPath(std::vector<EncoderWaiter>& waiters,
                                               const QString& ffmpegPath) {
    std::vector<EncoderWaiter> taken;
    for (auto iterator = waiters.begin(); iterator != waiters.end();) {
        if (iterator->ffmpegPath == ffmpegPath) {
            taken.push_back(std::move(*iterator));
            iterator = waiters.erase(iterator);
        } else {
            ++iterator;
        }
    }
    return taken;
}

void queueAvailabilityCallback(QObject* context,
                               std::function<void(bool)> callback,
                               bool h264VideoToolboxAvailable) {
    if (!context || !callback) {
        return;
    }
    QMetaObject::invokeMethod(
        context,
        [callback = std::move(callback), h264VideoToolboxAvailable]() {
            callback(h264VideoToolboxAvailable);
        },
        Qt::QueuedConnection);
}

void deliverBatches(std::vector<DeliveryBatch> batches) {
    for (DeliveryBatch& batch : batches) {
        for (EncoderWaiter& waiter : batch.waiters) {
            QObject* const context = waiter.context.data();
            queueAvailabilityCallback(context, std::move(waiter.callback), batch.available);
        }
    }
}

class EncoderProbeDispatcher : public QObject {
public:
    explicit EncoderProbeDispatcher(QObject* parent) : QObject(parent) {}

    ~EncoderProbeDispatcher() override {
        discardProbeProcess(/*deleteImmediately=*/true);
        EncoderAvailabilityState& state = availabilityState();
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.dispatcher == this) {
            state.dispatcher = nullptr;
        }
    }

    void startProbe(const QString& ffmpegPath) {
        discardProbeProcess(/*deleteImmediately=*/false);
        const int generation = claimProbeGeneration(ffmpegPath);
        probeProcess_ = std::make_unique<QProcess>();
        QProcess* const process = probeProcess_.get();
        process->setStandardInputFile(QProcess::nullDevice());
        connect(process, &QProcess::errorOccurred, this,
                [this, ffmpegPath, generation](QProcess::ProcessError error) {
                    if (error != QProcess::FailedToStart) {
                        return;
                    }
                    const QString reason = processFailureReason(
                        QStringLiteral("Failed to start ffmpeg -encoders."));
                    finishProbe(ffmpegPath, generation, /*succeeded=*/false, {}, reason);
                });
        connect(process,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this,
                [this, ffmpegPath, generation](int exitCode, QProcess::ExitStatus exitStatus) {
                    const bool succeeded = exitStatus == QProcess::NormalExit && exitCode == 0;
                    QString listing;
                    QString failureReason;
                    if (succeeded && probeProcess_) {
                        listing = QString::fromUtf8(probeProcess_->readAllStandardOutput());
                        listing += QLatin1Char('\n');
                        listing += QString::fromUtf8(probeProcess_->readAllStandardError());
                    } else if (!succeeded) {
                        const QString stderrOutput = probeProcess_
                            ? QString::fromUtf8(probeProcess_->readAllStandardError()).trimmed()
                            : QString();
                        failureReason = stderrOutput.isEmpty()
                            ? QStringLiteral("ffmpeg -encoders exited %1.").arg(exitCode)
                            : stderrOutput.right(500);
                    }
                    finishProbe(ffmpegPath, generation, succeeded, listing, failureReason);
                });
        process->start(ffmpegPath,
                       {QStringLiteral("-hide_banner"), QStringLiteral("-encoders")});
    }

private:
    int claimProbeGeneration(const QString& ffmpegPath) {
        EncoderAvailabilityState& state = availabilityState();
        std::lock_guard<std::mutex> lock(state.mutex);
        state.probeGeneration += 1;
        state.probeInFlight = true;
        state.probeFfmpegPath = ffmpegPath;
        return state.probeGeneration;
    }

    QString processFailureReason(const QString& fallback) const {
        if (!probeProcess_) {
            return fallback;
        }
        const QString errorString = probeProcess_->errorString().trimmed();
        if (errorString.isEmpty()) {
            return fallback;
        }
        return fallback + QLatin1Char(' ') + errorString;
    }

    void discardProbeProcess(bool deleteImmediately) {
        if (!probeProcess_) {
            return;
        }
        QProcess* const dyingProcess = probeProcess_.release();
        dyingProcess->disconnect();
        if (dyingProcess->state() != QProcess::NotRunning) {
            dyingProcess->kill();
            if (deleteImmediately) {
                dyingProcess->waitForFinished(3000);
            }
        }
        if (deleteImmediately) {
            delete dyingProcess;
        } else {
            dyingProcess->deleteLater();
        }
    }

    void finishProbe(const QString& ffmpegPath,
                     int generation,
                     bool succeeded,
                     const QString& encodersListing,
                     const QString& failureReason) {
        const bool available = succeeded
            && ExportVideoEncoder::encodersListingIncludesH264VideoToolbox(encodersListing);
        std::vector<DeliveryBatch> batches;
        QString nextProbePath;
        {
            EncoderAvailabilityState& state = availabilityState();
            std::lock_guard<std::mutex> lock(state.mutex);
            if (!state.probeInFlight || state.probeGeneration != generation
                || state.probeFfmpegPath != ffmpegPath) {
                return;
            }
            state.probeInFlight = false;
            if (succeeded) {
                state.hasResult = true;
                state.cachedFfmpegPath = ffmpegPath;
                state.h264VideoToolboxAvailable = available;
            }
            DeliveryBatch completed;
            completed.available = available;
            completed.waiters = takeWaitersForPath(state.waiters, ffmpegPath);
            if (!completed.waiters.empty()) {
                batches.push_back(std::move(completed));
            }
            while (!state.waiters.empty() && !state.probeInFlight) {
                const QString pendingPath = state.waiters.front().ffmpegPath;
                if (state.hasResult && state.cachedFfmpegPath == pendingPath) {
                    DeliveryBatch cached;
                    cached.available = state.h264VideoToolboxAvailable;
                    cached.waiters = takeWaitersForPath(state.waiters, pendingPath);
                    if (!cached.waiters.empty()) {
                        batches.push_back(std::move(cached));
                    }
                    continue;
                }
                state.probeInFlight = true;
                state.probeFfmpegPath = pendingPath;
                nextProbePath = pendingPath;
                break;
            }
        }

        discardProbeProcess(/*deleteImmediately=*/false);
        if (!succeeded) {
            qWarning("ExportVideoEncoder: %s", qPrintable(failureReason));
        }
        deliverBatches(std::move(batches));
        if (!nextProbePath.isEmpty()) {
            startProbe(nextProbePath);
        }
    }

    std::unique_ptr<QProcess> probeProcess_;
};

EncoderProbeDispatcher* sharedDispatcher() {
    QCoreApplication* const application = QCoreApplication::instance();
    Q_ASSERT(application != nullptr);
    EncoderAvailabilityState& state = availabilityState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.dispatcher) {
        state.dispatcher = new EncoderProbeDispatcher(application);
    }
    return state.dispatcher;
}

}  // namespace

namespace ExportVideoEncoder {

bool encodersListingIncludesH264VideoToolbox(const QString& encodersListing) {
    QString normalized = encodersListing;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QRegularExpression encoderLine(
        QStringLiteral(R"(^\s*V\S*\s+h264_videotoolbox\b)"),
        QRegularExpression::MultilineOption);
    return encoderLine.match(normalized).hasMatch();
}

Kind kindForClip(bool h264VideoToolboxAvailable, bool hardwareEncodeFailedEarlier) {
    if (h264VideoToolboxAvailable && !hardwareEncodeFailedEarlier) {
        return Kind::H264VideoToolbox;
    }
    return Kind::Libx264;
}

bool shouldRetryWithLibx264(Kind kindUsed) {
    return kindUsed == Kind::H264VideoToolbox;
}

QStringList sourceDecodeArguments(Kind kind) {
    if (kind != Kind::H264VideoToolbox) {
        return {};
    }
    return {
        QStringLiteral("-hwaccel"),
        QStringLiteral("videotoolbox"),
    };
}

QStringList codecArguments(Kind kind) {
    if (kind == Kind::H264VideoToolbox) {
        return {
            QStringLiteral("-c:v"),
            QStringLiteral("h264_videotoolbox"),
            QStringLiteral("-q:v"),
            QString::number(kVideoToolboxQuality),
        };
    }
    return {
        QStringLiteral("-c:v"),
        QStringLiteral("libx264"),
        QStringLiteral("-preset"),
        QStringLiteral("fast"),
        QStringLiteral("-crf"),
        QStringLiteral("23"),
    };
}

QStringList containerCompatibilityArguments() {
    return {
        QStringLiteral("-pix_fmt"),
        QStringLiteral("yuv420p"),
        QStringLiteral("-movflags"),
        QStringLiteral("+faststart"),
    };
}

void requestH264VideoToolboxAvailability(
    const QString& ffmpegPath,
    QObject* context,
    std::function<void(bool h264VideoToolboxAvailable)> callback) {
    if (!context) {
        return;
    }
    if (ffmpegPath.isEmpty() || QCoreApplication::instance() == nullptr) {
        queueAvailabilityCallback(context, std::move(callback), false);
        return;
    }

    EncoderProbeDispatcher* const dispatcher = sharedDispatcher();
    bool cached = false;
    bool available = false;
    bool shouldStart = false;
    {
        EncoderAvailabilityState& state = availabilityState();
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.hasResult && state.cachedFfmpegPath == ffmpegPath) {
            cached = true;
            available = state.h264VideoToolboxAvailable;
        } else {
            EncoderWaiter waiter;
            waiter.context = context;
            waiter.ffmpegPath = ffmpegPath;
            waiter.callback = std::move(callback);
            state.waiters.push_back(std::move(waiter));
            if (!state.probeInFlight) {
                state.probeInFlight = true;
                state.probeFfmpegPath = ffmpegPath;
                shouldStart = true;
            }
        }
    }

    if (cached) {
        queueAvailabilityCallback(context, std::move(callback), available);
        return;
    }
    if (shouldStart) {
        dispatcher->startProbe(ffmpegPath);
    }
}

}  // namespace ExportVideoEncoder
