#include "MainWindow.h"

#include <QAudioOutput>
#include <QFileDialog>
#include <QLabel>
#include <QMediaPlayer>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QWidget>


MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("AVA - asiatica video analyzer");
    resize(900, 600);

    buildUi();
    wireSignals();
}

void MainWindow::buildUi() {
    // central widget and its layout:
    auto* central = new QWidget(this);
    setCentralWidget(central);

    auto* layout = new QVBoxLayout(central);
    layout-> setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);

    // header:
    headerLabel_ = new QLabel("this is ava", central);
    headerLabel_->setWordWrap(true);
    headerLabel_->setStyleSheet(
        "font-size: 28px;"
        "font-weight: 700;"
    );

    // subtitle:
    subtitleLabel_ = new QLabel(
        "Import a video file to get started",
        central
    );
    subtitleLabel_->setWordWrap(true);
    subtitleLabel_->setStyleSheet(
        "font-size: 16px;"
        "color: #9A8555"
    );

    // import button:
    importButton_ = new QPushButton("Select video file", central);
    importButton_->setCursor(Qt::PointingHandCursor);
    importButton_->setStyleSheet(
        "padding: 10px 14px;"
        "font-size: 14px;"
    );

    // video widget (initially hidden):
    videoWidget_ = new QVideoWidget(central);
    videoWidget_->setMinimumHeight(360);
    videoWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoWidget_->hide();

    // media player and audio output:
    player_ = new QMediaPlayer(this);
    audioOutput_ = new QAudioOutput(this);
    player_->setAudioOutput(audioOutput_);
    player_->setVideoOutput(videoWidget_);

    // add components to the layout:
    layout->addWidget(headerLabel_);
    layout->addWidget(subtitleLabel_);
    layout->addSpacing(8);
    layout->addWidget(importButton_);
    layout->addWidget(videoWidget_, /*stretch=*/1);

}

void MainWindow::wireSignals() {
    connect(importButton_, &QPushButton::clicked,
        this, &MainWindow::onImportVideoClicked);
}

void MainWindow::onImportVideoClicked() {
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        "Select a video file",
        QString(),
        "Video files (*.mp4 *.mov *.m4v *.mkv *.avi);;All files (*.*)"
    );

    if (filePath.isEmpty()) {
        return; // user cancelled
    }

    // hide import button and reveal video widget:
    importButton_->hide();
    videoWidget_->show();

    // load and play:
    player_->setSource(QUrl::fromLocalFile(filePath));
    player_->play();
}