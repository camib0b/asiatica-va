#pragma once

#include <QColor>
#include <QString>
#include <QVector>
#include <QWidget>

class QAbstractButton;
class QEvent;
class QFrame;
class QLineEdit;
class QObject;
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
  void applyUiStrings();

signals:
  void colorChanged(const QString& hex);

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
  void onWellClicked();
  void onHexTextChanged(const QString& text);
  void onHexEditingFinished();
  void onMoreColorsClicked();

private:
  void onPopupHide();
  void buildUi();
  void showPalettePopup();
  void hidePalettePopup();
  void applyNormalizedColor(const QString& normalizedHex, bool emitChange);
  void refreshWell();
  void refreshSwatchSelection();
  void syncHexEditFromColor();
  QColor dialogSeedColor() const;

  static QString colorToHex(const QColor& color);
  static QString normalizeHex(const QString& text);

  QString colorHex_;
  QString colorDialogTitle_;
  QColor fallbackPreviewColor_ = QColor(Qt::gray);
  bool popupJustClosed_ = false;
  bool syncingHexEdit_ = false;

  QAbstractButton* wellButton_ = nullptr;
  QFrame* popup_ = nullptr;
  QLineEdit* hexEdit_ = nullptr;
  QPushButton* moreColorsButton_ = nullptr;
  QVector<QAbstractButton*> swatchButtons_;
};
