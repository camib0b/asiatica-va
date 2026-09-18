#pragma once

#include <QDialog>
#include <QPointer>
#include <QString>
#include <QVector>

class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QPushButton;
class QWidget;
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
    QPointer<QLabel> eventLabel;
    QPointer<QDoubleSpinBox> leadSpin;
    QPointer<QDoubleSpinBox> lagSpin;
    QPointer<QLabel> totalLabel;
  };

  void buildUi();
  void populateRows();
  void refreshTotalLabel(const DurationRow& row);
  void applyDurationToSession(const QString& eventName, qint64 leadMs, qint64 lagMs);

  TagSession* tagSession_ = nullptr;
  QVector<DurationRow> rows_;

  QPointer<QWidget> tableHost_;
  QPointer<QGridLayout> tableGrid_;
  QPointer<QLabel> titleLabel_;
  QPointer<QLabel> subtitleLabel_;
  QPointer<QLabel> eventHeaderLabel_;
  QPointer<QLabel> leadHeaderLabel_;
  QPointer<QLabel> lagHeaderLabel_;
  QPointer<QLabel> totalHeaderLabel_;
  QPointer<QPushButton> resetButton_;
  QPointer<QPushButton> closeButton_;
};
