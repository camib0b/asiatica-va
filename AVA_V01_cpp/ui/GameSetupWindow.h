#pragma once

#include <QDate>
#include <QString>
#include <QWidget>

class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class TeamColorPicker;

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
  void onCompetitionTextChanged(const QString& text);
  void onGameDateChanged(QDate date);

private:
  void buildUi();
  void wireSignals();
  void onContinue();
  void onBack();
  void updateOptionalFieldAppearance();
  /// Returns the first 3 alphanumeric characters of \p teamName, uppercased.
  /// Falls back to empty string when the team name has no alphanumeric content.
  static QString deriveAbbreviationFromTeamName(const QString& teamName);

  QString videoPath_;
  bool ignoreDateChange_ = false;
  bool dateEditedByUser_ = false;

  QLabel* titleLabel_ = nullptr;
  QLabel* homeTeamLabel_ = nullptr;
  QLabel* awayTeamLabel_ = nullptr;
  QLabel* optionalLabel_ = nullptr;
  QLabel* competitionLabel_ = nullptr;
  QLabel* dateLabel_ = nullptr;
  QLineEdit* homeNameEdit_ = nullptr;
  QLineEdit* awayNameEdit_ = nullptr;
  QLineEdit* homeAbbrevEdit_ = nullptr;
  QLineEdit* awayAbbrevEdit_ = nullptr;
  QLineEdit* competitionEdit_ = nullptr;
  QDateEdit* gameDateEdit_ = nullptr;
  TeamColorPicker* homeColorPicker_ = nullptr;
  TeamColorPicker* awayColorPicker_ = nullptr;
  QComboBox* languageCombo_ = nullptr;
  QPushButton* continueButton_ = nullptr;
  QPushButton* backButton_ = nullptr;
};
