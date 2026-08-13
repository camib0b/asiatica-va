#include "XmlSyncDialog.h"

#include "../components/VideoPlayer.h"
#include "../i18n/AppLocale.h"
#include "../style/StyleProps.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

XmlSyncDialog::XmlSyncDialog(VideoPlayer* videoPlayer,
                             const XmlImporter::ParsedInstance& anchorInstance,
                             const QVector<XmlImporter::ParsedInstance>& instances,
                             bool anchorUsedFallback,
                             QWidget* parent)
    : QDialog(parent),
      videoPlayer_(videoPlayer),
      anchorInstance_(anchorInstance),
      instances_(instances),
      anchorUsedFallback_(anchorUsedFallback) {
  setWindowTitle(AppLocale::trUi("xml_import.sync_title"));
  setMinimumWidth(520);
  buildUi();
  wireSignals();
  applyUiStrings();
  updatePreview();
}

void XmlSyncDialog::buildUi() {
  auto* layout = new QVBoxLayout(this);
  layout->setSpacing(12);

  titleLabel_ = new QLabel(this);
  Style::setRole(titleLabel_, "h2");
  layout->addWidget(titleLabel_);

  instructionsLabel_ = new QLabel(this);
  instructionsLabel_->setWordWrap(true);
  layout->addWidget(instructionsLabel_);

  fallbackWarningLabel_ = new QLabel(this);
  fallbackWarningLabel_->setWordWrap(true);
  Style::setRole(fallbackWarningLabel_, "muted");
  layout->addWidget(fallbackWarningLabel_);

  xmlAnchorLabel_ = new QLabel(this);
  layout->addWidget(xmlAnchorLabel_);

  videoAnchorLabel_ = new QLabel(this);
  layout->addWidget(videoAnchorLabel_);

  useCurrentButton_ = new QPushButton(this);
  Style::setVariant(useCurrentButton_, "secondary");
  layout->addWidget(useCurrentButton_, 0, Qt::AlignLeft);

  offsetLabel_ = new QLabel(this);
  layout->addWidget(offsetLabel_);

  previewLabel_ = new QLabel(this);
  previewLabel_->setWordWrap(true);
  layout->addWidget(previewLabel_);

  auto* buttonRow = new QHBoxLayout();
  buttonRow->addStretch();
  cancelButton_ = new QPushButton(this);
  importButton_ = new QPushButton(this);
  Style::setVariant(importButton_, "primary");
  importButton_->setDefault(true);
  buttonRow->addWidget(cancelButton_);
  buttonRow->addWidget(importButton_);
  layout->addLayout(buttonRow);
}

void XmlSyncDialog::wireSignals() {
  connect(cancelButton_, &QPushButton::clicked, this, &QDialog::reject);
  connect(importButton_, &QPushButton::clicked, this, &QDialog::accept);
  connect(useCurrentButton_, &QPushButton::clicked, this, &XmlSyncDialog::onUseCurrentPositionClicked);

  if (videoPlayer_) {
    connect(videoPlayer_, &VideoPlayer::positionChangedMs, this,
            &XmlSyncDialog::onVideoPositionChanged);
    onVideoPositionChanged(videoPlayer_->currentPositionMs());
  }
}

void XmlSyncDialog::applyUiStrings() {
  titleLabel_->setText(AppLocale::trUi("xml_import.sync_title"));
  instructionsLabel_->setText(AppLocale::trUi("xml_import.sync_instructions"));
  fallbackWarningLabel_->setText(anchorUsedFallback_
                                     ? AppLocale::trUi("xml_import.sync_fallback_warning")
                                     : QString());
  fallbackWarningLabel_->setVisible(anchorUsedFallback_);
  xmlAnchorLabel_->setText(
      AppLocale::trUi("xml_import.sync_xml_anchor")
          .arg(formatMs(anchorInstance_.startMs))
          .arg(anchorInstance_.code));
  useCurrentButton_->setText(AppLocale::trUi("xml_import.sync_use_playhead"));
  cancelButton_->setText(AppLocale::trUi("xml_import.cancel"));
  importButton_->setText(AppLocale::trUi("xml_import.continue"));
  updatePreview();
}

QString XmlSyncDialog::formatMs(qint64 ms) {
  if (ms < 0) ms = 0;
  const qint64 totalSeconds = ms / 1000;
  const qint64 minutes = totalSeconds / 60;
  const qint64 seconds = totalSeconds % 60;
  const qint64 millis = ms % 1000;
  return QStringLiteral("%1:%2.%3")
      .arg(minutes, 2, 10, QChar('0'))
      .arg(seconds, 2, 10, QChar('0'))
      .arg(millis, 3, 10, QChar('0'));
}

void XmlSyncDialog::onVideoPositionChanged(qint64 positionMs) {
  offsetMs_ = positionMs - anchorInstance_.startMs;
  updatePreview();
}

void XmlSyncDialog::onUseCurrentPositionClicked() {
  if (videoPlayer_) {
    onVideoPositionChanged(videoPlayer_->currentPositionMs());
  }
}

void XmlSyncDialog::updatePreview() {
  if (videoAnchorLabel_) {
    const qint64 videoMs = anchorInstance_.startMs + offsetMs_;
    videoAnchorLabel_->setText(AppLocale::trUi("xml_import.sync_video_anchor").arg(formatMs(videoMs)));
  }
  if (offsetLabel_) {
    const double offsetSeconds = static_cast<double>(offsetMs_) / 1000.0;
    offsetLabel_->setText(AppLocale::trUi("xml_import.sync_offset").arg(offsetSeconds, 0, 'f', 3));
  }
  if (previewLabel_) {
    const int beforeZero = countClampedBeforeZero();
    const int afterDuration = countClampedAfterDuration();
    if (beforeZero == 0 && afterDuration == 0) {
      previewLabel_->setText(AppLocale::trUi("xml_import.sync_preview_ok")
                                 .arg(instances_.size()));
    } else {
      previewLabel_->setText(AppLocale::trUi("xml_import.sync_preview_clamp")
                                 .arg(instances_.size())
                                 .arg(beforeZero)
                                 .arg(afterDuration));
    }
  }
}

int XmlSyncDialog::countClampedBeforeZero() const {
  int count = 0;
  for (const XmlImporter::ParsedInstance& instance : instances_) {
    if (instance.startMs + offsetMs_ < 0) ++count;
  }
  return count;
}

int XmlSyncDialog::countClampedAfterDuration() const {
  if (!videoPlayer_) return 0;
  const qint64 durationMs = videoPlayer_->durationMs();
  if (durationMs <= 0) return 0;
  int count = 0;
  for (const XmlImporter::ParsedInstance& instance : instances_) {
    if (instance.endMs + offsetMs_ > durationMs) ++count;
  }
  return count;
}
