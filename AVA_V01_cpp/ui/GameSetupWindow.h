#pragma once

#include <QDate>
#include <QString>
#include <QWidget>

class QComboBox;
class QDateEdit;
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
  void setMetadataDefaults(const QString& competitionName,
                           const QDate& gameDate,
                           const QString& homeAbbrev,
                           const QString& awayAbbrev);
  void setInitialFocus();

  void applyUiStrings();

signals:
  void teamSetupConfirmed(const QString& filePath,
                          const QString& homeName, const QString& awayName,
                          const QString& homeColor, const QString& awayColor,
                          const QString& competitionName,
                          const QDate& gameDate,
                          const QString& homeAbbrev,
                          const QString& awayAbbrev);
  void cancelled();

private slots:
  void onLanguageComboChanged(int index);
  void onHomeNameEditingFinished();
  void onAwayNameEditingFinished();

private:
  void buildUi();
  void wireSignals();
  void onContinue();
  void onBack();
  void onHomeColorPick();
  void onAwayColorPick();
  static QString colorToHex(const QColor& c);
  /// Returns the first 3 alphanumeric characters of \p teamName, uppercased.
  /// Falls back to empty string when the team name has no alphanumeric content.
  static QString deriveAbbreviationFromTeamName(const QString& teamName);

  QString videoPath_;
  QLabel* titleLabel_ = nullptr;
  QLabel* homeTeamLabel_ = nullptr;
  QLabel* awayTeamLabel_ = nullptr;
  QLabel* homeColorLabel_ = nullptr;
  QLabel* awayColorLabel_ = nullptr;
  QLabel* homeAbbrevLabel_ = nullptr;
  QLabel* awayAbbrevLabel_ = nullptr;
  QLabel* competitionLabel_ = nullptr;
  QLabel* dateLabel_ = nullptr;
  QLabel* languageLabel_ = nullptr;
  QLineEdit* homeNameEdit_ = nullptr;
  QLineEdit* awayNameEdit_ = nullptr;
  QLineEdit* homeColorEdit_ = nullptr;
  QLineEdit* awayColorEdit_ = nullptr;
  QLineEdit* homeAbbrevEdit_ = nullptr;
  QLineEdit* awayAbbrevEdit_ = nullptr;
  QLineEdit* competitionEdit_ = nullptr;
  QDateEdit* gameDateEdit_ = nullptr;
  QPushButton* homeColorButton_ = nullptr;
  QPushButton* awayColorButton_ = nullptr;
  QComboBox* languageCombo_ = nullptr;
  QPushButton* continueButton_ = nullptr;
  QPushButton* backButton_ = nullptr;
};
