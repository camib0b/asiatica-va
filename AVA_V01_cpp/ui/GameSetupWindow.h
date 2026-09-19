#pragma once

#include <QDate>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <memory>

class GameMetadataSuggester;
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
  ~GameSetupWindow() override;

  void setVideoPath(const QString& path);
  QString videoPath() const { return videoPath_; }
  void setTeamDefaults(const QString& homeName, const QString& awayName,
                       const QString& homeColor, const QString& awayColor);
  void setMetadataDefaults(const QDate& gameDate,
                           const QString& homeAbbrev,
                           const QString& awayAbbrev);
  void beginMetadataSuggestion(const QStringList& sourceVideoPaths);
  void setInitialFocus();

  void applyUiStrings() const;

signals:
  void gameSetupConfirmed(const QString& filePath,
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
  void onGameDateChanged(QDate date);
  void onNameDateSuggested(const QString& homeTeamName,
                           const QString& awayTeamName,
                           const QDate& gameDate);
  void onColorDetectionStarted();
  void onColorsSuggested(const QString& homeColorHex, const QString& awayColorHex);
  void onMetadataSuggestionFinished();

private:
  void buildUi();
  void wireSignals();
  void onContinue();
  void onBack();
  void updateOptionalFieldAppearance() const;
  enum class OptionalDateCommit {
    Placeholder,
    Chosen,
  };
  void applyOptionalDate(const QDate& date, OptionalDateCommit commit);
  void updateContinueButtonEnabled() const;
  struct SetupFormValues {
    QString homeName;
    QString awayName;
    QString homeColor;
    QString awayColor;
    QString homeAbbrev;
    QString awayAbbrev;
    QDate gameDate;
  };
  SetupFormValues collectSetupFormValues() const;
  bool hasRequiredSetupFields(const SetupFormValues& values) const;
  void abortMetadataSuggestion();
  void connectMetadataSuggester();
  void disconnectMetadataSuggester();
  void discardMetadataSuggester();
  bool suggestionSignalsArmed() const;
  void setSuggestionStatusKey(const char* key);
  /// Returns the first 3 alphanumeric characters of \p teamName, uppercased.
  /// Falls back to empty string when the team name has no alphanumeric content.
  static QString deriveAbbreviationFromTeamName(const QString& teamName);

  QString videoPath_;
  bool optionalDateChosen_ = false;
  bool suggestionSignalsArmed_ = false;
  const char* suggestionStatusKey_ = nullptr;

  std::unique_ptr<GameMetadataSuggester> metadataSuggester_;
  QVector<QMetaObject::Connection> metadataSuggesterConnections_;
  QPointer<QLabel> titleLabel_;
  QPointer<QLabel> suggestionStatusLabel_;
  QPointer<QLabel> homeTeamLabel_;
  QPointer<QLabel> awayTeamLabel_;
  QPointer<QLabel> optionalLabel_;
  QPointer<QLabel> dateLabel_;
  QPointer<QLineEdit> homeNameEdit_;
  QPointer<QLineEdit> awayNameEdit_;
  QPointer<QLineEdit> homeAbbrevEdit_;
  QPointer<QLineEdit> awayAbbrevEdit_;
  QPointer<QDateEdit> gameDateEdit_;
  QPointer<TeamColorPicker> homeColorPicker_;
  QPointer<TeamColorPicker> awayColorPicker_;
  QPointer<QComboBox> languageCombo_;
  QPointer<QPushButton> continueButton_;
  QPointer<QPushButton> backButton_;
};
