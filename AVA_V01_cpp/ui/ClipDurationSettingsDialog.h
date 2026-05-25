#pragma once

#include <QDialog>
#include <QString>
#include <QVector>

class QDoubleSpinBox;
class QLabel;
class QPushButton;
class TagSession;

class ClipDurationSettingsDialog final : public QDialog {
  Q_OBJECT

public:
  explicit ClipDurationSettingsDialog(TagSession* session, QWidget* parent = nullptr);

  void applyUiStrings();

private slots:
  void onDurationChanged(const QString& eventName);
  void onResetAllClicked();

private:
  struct DurationRow {
    QString eventName;
    QLabel* eventLabel = nullptr;
    QDoubleSpinBox* leadSpin = nullptr;
    QDoubleSpinBox* lagSpin = nullptr;
    QLabel* totalLabel = nullptr;
  };

  void buildUi();
  void populateRows();
  void refreshTotalLabel(const DurationRow& row);
  void applyDurationToSession(const QString& eventName, qint64 preMs, qint64 postMs);

  TagSession* tagSession_ = nullptr;
  QVector<DurationRow> rows_;

  QLabel* titleLabel_ = nullptr;
  QLabel* subtitleLabel_ = nullptr;
  QLabel* eventHeaderLabel_ = nullptr;
  QLabel* leadHeaderLabel_ = nullptr;
  QLabel* lagHeaderLabel_ = nullptr;
  QLabel* totalHeaderLabel_ = nullptr;
  QPushButton* resetButton_ = nullptr;
  QPushButton* closeButton_ = nullptr;
};
