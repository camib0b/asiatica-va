#pragma once

#include <QColor>
#include <QString>
#include <QVector>
#include <QWidget>

class QAbstractButton;
class QEvent;
class QFrame;
class QLineEdit;
class QPushButton;

/// Compact color well that opens a two-click palette (swatches, hex, color wheel).
class TeamColorPicker final : public QWidget {
  Q_OBJECT

public:
  explicit TeamColorPicker(QWidget* parent = nullptr);

  void setColor(const QString& hex);
  QString color() const { return colorHex_; }

  void setColorDialogTitle(const QString& title);
  void setFallbackPreviewColor(const QColor& color);
  void applyUiStrings() const;
  bool eventFilter(QObject* watched, QEvent* event) override;

signals:
  void colorChanged(const QString& hex);

private slots:
  void onWellClicked();
  void onHexTextChanged(const QString& text);
  void onHexEditingFinished();
  void onMoreColorsClicked();

private:
  void buildUi();
  void showPalettePopup();
  void hidePalettePopup();
  QVector<QWidget*> popupKeyboardFocusChain() const;
  bool movePopupKeyboardFocus(bool forward);
  void applyNormalizedColor(const QString& normalizedHex, bool emitChange);
  void refreshWell() const;
  void refreshSwatchSelection() const;
  void syncHexEditFromColor() const;
  QColor dialogSeedColor() const;

  QString colorHex_{};
  QString colorDialogTitle_{};
  QColor fallbackPreviewColor_ = QColor(Qt::gray);
  mutable bool syncingHexEdit_ = false;

  QAbstractButton* wellButton_ = nullptr;
  QFrame* popup_ = nullptr;
  QLineEdit* hexEdit_ = nullptr;
  QPushButton* moreColorsButton_ = nullptr;
  QVector<QAbstractButton*> swatchButtons_{};
};
