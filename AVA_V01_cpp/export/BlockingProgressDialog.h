#pragma once

#include "StyleProps.h"
#include "TextFit.h"

#include <QLabel>
#include <QProgressDialog>
#include <QPushButton>
#include <QStyle>
#include <QStyleOption>

#include <memory>

namespace BlockingProgress {

/// Sizes a modal progress dialog so its one-line message and Cancel button
/// keep their full text. Uses ElidingLabel's size hint (the text-fit label)
/// and style margins, then grows again after the dialog has been laid out.
inline void fitDialog(QProgressDialog& dialog, const QString& message) {
    QLabel* messageLabel = nullptr;
    const QList<QLabel*> labels = dialog.findChildren<QLabel*>();
    for (QLabel* label : labels) {
        if (label && label->text() == message) {
            messageLabel = label;
            break;
        }
    }

    if (!messageLabel) {
        auto created = std::make_unique<Style::ElidingLabel>(message);
        created->setTextFormat(Qt::PlainText);
        if (dialog.style() != nullptr) {
            const int alignment = dialog.style()->styleHint(
                QStyle::SH_ProgressDialog_TextLabelAlignment, nullptr, &dialog);
            created->setAlignment(static_cast<Qt::Alignment>(alignment));
        }
        messageLabel = created.get();
        dialog.setLabel(created.release());
    }

    if (QPushButton* cancelButton = dialog.findChild<QPushButton*>()) {
        const int fittedButtonWidth = Style::pushButtonMinimumWidth(cancelButton);
        if (cancelButton->minimumWidth() < fittedButtonWidth) {
            cancelButton->setMinimumWidth(fittedButtonWidth);
        }
    }

    dialog.ensurePolished();
    messageLabel->ensurePolished();

    const QFontMetrics metrics = messageLabel->fontMetrics();
    const int textWidth = qMax(metrics.horizontalAdvance(message) + 1,
                               metrics.size(Qt::TextSingleLine, message).width());
    QStyleOption option;
    option.initFrom(&dialog);
    const int marginLeft =
        dialog.style()->pixelMetric(QStyle::PM_LayoutLeftMargin, &option, &dialog);
    const int marginRight =
        dialog.style()->pixelMetric(QStyle::PM_LayoutRightMargin, &option, &dialog);
    const int fittedWidth = textWidth + marginLeft + marginRight;
    const QSize hint = dialog.sizeHint().expandedTo(QSize(fittedWidth, 0));
    dialog.setMinimumWidth(qMax(dialog.minimumWidth(), hint.width()));
    dialog.resize(dialog.size().expandedTo(hint));

    if (messageLabel->width() <= 0) return;

    const int visibleWidth = messageLabel->contentsRect().width();
    const int shortfall = textWidth - visibleWidth;
    if (shortfall > 0) {
        dialog.resize(dialog.width() + shortfall, qMax(dialog.height(), hint.height()));
    }
}

}  // namespace BlockingProgress
