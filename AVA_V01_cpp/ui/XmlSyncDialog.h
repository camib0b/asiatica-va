#pragma once

#include "../export/XmlImporter.h"

#include <QDialog>
#include <QtGlobal>

class QLabel;
class QPushButton;
class VideoPlayer;

class XmlSyncDialog final : public QDialog {
  Q_OBJECT

public:
  XmlSyncDialog(VideoPlayer* videoPlayer,
                const XmlImporter::ParsedInstance& anchorInstance,
                const QVector<XmlImporter::ParsedInstance>& instances,
                bool anchorUsedFallback,
                QWidget* parent = nullptr);

  qint64 offsetMs() const { return offsetMs_; }

  void applyUiStrings();

private slots:
  void onVideoPositionChanged(qint64 positionMs);
  void onUseCurrentPositionClicked();
  void updatePreview();

private:
  void buildUi();
  void wireSignals();
  static QString formatMs(qint64 ms);
  int countClampedBeforeZero() const;
  int countClampedAfterDuration() const;

  VideoPlayer* videoPlayer_ = nullptr;
  XmlImporter::ParsedInstance anchorInstance_;
  QVector<XmlImporter::ParsedInstance> instances_;
  bool anchorUsedFallback_ = false;
  qint64 offsetMs_ = 0;

  QLabel* titleLabel_ = nullptr;
  QLabel* instructionsLabel_ = nullptr;
  QLabel* xmlAnchorLabel_ = nullptr;
  QLabel* videoAnchorLabel_ = nullptr;
  QLabel* offsetLabel_ = nullptr;
  QLabel* previewLabel_ = nullptr;
  QLabel* fallbackWarningLabel_ = nullptr;
  QPushButton* useCurrentButton_ = nullptr;
  QPushButton* importButton_ = nullptr;
  QPushButton* cancelButton_ = nullptr;
};
