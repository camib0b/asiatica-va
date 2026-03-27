#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

class QWidget;

class VideoConcatenator : public QObject {
    Q_OBJECT

public:
    explicit VideoConcatenator(QObject* parent = nullptr);
    ~VideoConcatenator() override;

    void startConcatenation(const QStringList& inputPaths, const QString& outputDir);
    void cancel();

    bool isRunning() const;
    bool isFinished() const { return finished_; }
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

private:
    QProcess* process_ = nullptr;
    QString outputPath_;
    QString errorMessage_;
    bool finished_ = false;
    bool succeeded_ = false;
    bool cancelled_ = false;
};
