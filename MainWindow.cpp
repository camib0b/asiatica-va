// Updated MainWindow.cpp
#include "MainWindow.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    // Set window title
    setWindowTitle("Asiatica VA (AVA)");

    // Create central widget with layout
    centralWidget = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(centralWidget);

    // Add text label (practice adding text)
    welcomeLabel = new QLabel("Welcome to Asiatica VA!", centralWidget);
    welcomeLabel->setAlignment(Qt::AlignCenter);
    welcomeLabel->setStyleSheet("font-size: 24px; color: white; background-color: blue;"); // Styled for visibility
    layout->addWidget(welcomeLabel);

    // Video widget for displaying video
    videoWidget = new QVideoWidget(centralWidget);
    layout->addWidget(videoWidget, 1); // Stretch to take more space

    // Add button object (practice adding interactive objects)
    startButton = new QPushButton("Start Analysis", centralWidget);
    connect(startButton, &QPushButton::clicked, this, &MainWindow::onButtonClicked);
    layout->addWidget(startButton);

    // Add slider object (e.g., for volume control practice)
    controlSlider = new QSlider(Qt::Horizontal, centralWidget);
    controlSlider->setRange(0, 100); // Example range
    controlSlider->setValue(50);     // Default value
    layout->addWidget(controlSlider);

    // Media player setup
    mediaPlayer = new QMediaPlayer(this);
    audioOutput = new QAudioOutput(this); // Create audio output
    mediaPlayer->setAudioOutput(audioOutput); // Assign to player
    mediaPlayer->setVideoOutput(videoWidget);

    // Connect slider to audio output volume (Qt 6 uses float 0.0-1.0, so use lambda to convert)
    connect(controlSlider, &QSlider::valueChanged, this, [this](int value) {
        audioOutput->setVolume(value / 100.0f);
    });

    setCentralWidget(centralWidget);

    // Menu bar setup
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));

    // Action for importing/opening video file
    QAction *openAction = new QAction(tr("&Open Video"), this);
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openVideoFile);
    fileMenu->addAction(openAction);

    // Optional: Exit action
    QAction *exitAction = new QAction(tr("E&xit"), this);
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &MainWindow::close);
    fileMenu->addAction(exitAction);

    // Resize window to a reasonable default
    resize(800, 600);
}

MainWindow::~MainWindow() {
    // Cleanup if needed
}

void MainWindow::openVideoFile() {
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open Video File"), "", tr("Video Files (*.mp4 *.avi *.mkv *.mov)"));
    if (!fileName.isEmpty()) {
        mediaPlayer->setSource(QUrl::fromLocalFile(fileName));
        mediaPlayer->play();
        welcomeLabel->hide(); // Hide welcome text once video loads (optional practice)
    } else {
        QMessageBox::warning(this, tr("Error"), tr("No file selected."));
    }
}

void MainWindow::onButtonClicked() {
    // Practice slot: Show a message when button is clicked
    QMessageBox::information(this, tr("Button Clicked"), tr("Analysis started! (Placeholder)"));
}