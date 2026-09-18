#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <memory>

class QWidget;

class VideoConcatenator : public QObject {
    Q_OBJECT

public:
    explicit VideoConcatenator(QObject* parent = nullptr);
    ~VideoConcatenator() override;

    void startConcatenation(const QStringList& inputPaths, const QString& outputDir);
    void cancel();

    bool succeeded() const { return succeeded_; }
    QString outputPath() const { return outputPath_; }
    QString errorMessage() const { return errorMessage_; }

    bool waitWithProgress(QWidget* parentWidget);

    static QStringList selectVideoFiles(QWidget* parentWidget);
    static bool showFileOrderDialog(QStringList& filePaths, QWidget* parentWidget);

signals:
    void concatenationFinished(bool success);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);

private:
    void stopAndDiscardProcess();
    void failWith(const QString& message);

    /// Unparented QObject; this unique_ptr is the only owner. Replace via
    /// stopAndDiscardProcess() (release + deleteLater) so finished() cannot
    /// delete the process on its own stack.
    std::unique_ptr<QProcess> process_;
    QString outputPath_;
    QString errorMessage_;
    bool finished_ = false;
    bool succeeded_ = false;
    bool cancelled_ = false;
};
