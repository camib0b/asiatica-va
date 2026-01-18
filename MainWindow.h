// Updated MainWindow.h
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMenuBar>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QVideoWidget>
#include <QMediaPlayer>
#include <QVBoxLayout>
#include <QWidget>
#include <QLabel>      // For text
#include <QPushButton> // For button object
#include <QSlider>     // For slider object
#include <QAudioOutput> // Add this for audio handling in Qt 6

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void openVideoFile();
    void onButtonClicked(); // Slot for button practice

private:
    QVideoWidget *videoWidget;
    QMediaPlayer *mediaPlayer;
    QAudioOutput *audioOutput; // Add this for volume control
    QWidget *centralWidget;
    QLabel *welcomeLabel;   // Text element
    QPushButton *startButton; // Button object
    QSlider *controlSlider;   // Slider object
};

#endif // MAINWINDOW_H