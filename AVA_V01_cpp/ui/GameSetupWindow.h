#pragma once

#include <QWidget>
#include <QString>

class QLabel;
class QLineEdit;
class QPushButton;

class GameSetupWindow final : public QWidget {
  Q_OBJECT

public:
  explicit GameSetupWindow(QWidget* parent = nullptr);
  ~GameSetupWindow() override = default;

  void setVideoPath(const QString& path);
  QString videoPath() const { return videoPath_; }
  void setTeamDefaults(const QString& homeName, const QString& awayName,
                       const QString& homeColor, const QString& awayColor);
  void setInitialFocus();

signals:
  void teamSetupConfirmed(const QString& filePath,
                          const QString& homeName, const QString& awayName,
                          const QString& homeColor, const QString& awayColor);
  void cancelled();

private:
  void buildUi();
  void wireSignals();
  void onContinue();
  void onBack();
  void onHomeColorPick();
  void onAwayColorPick();
  static QString colorToHex(const QColor& c);

  QString videoPath_;
  QLabel* titleLabel_ = nullptr;
  QLabel* subtitleLabel_ = nullptr;
  QLineEdit* homeNameEdit_ = nullptr;
  QLineEdit* awayNameEdit_ = nullptr;
  QLineEdit* homeColorEdit_ = nullptr;
  QLineEdit* awayColorEdit_ = nullptr;
  QPushButton* homeColorButton_ = nullptr;
  QPushButton* awayColorButton_ = nullptr;
  QPushButton* continueButton_ = nullptr;
  QPushButton* backButton_ = nullptr;
};
