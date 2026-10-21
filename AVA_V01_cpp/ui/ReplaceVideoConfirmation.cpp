#include "ReplaceVideoConfirmation.h"

#include "AppLocale.h"
#include "SessionNotesWriter.h"
#include "TagSession.h"

#include <QAbstractButton>
#include <QMessageBox>
#include <QPushButton>

namespace {

ReplaceVideoChoice confirmReplaceVideo(QWidget* parent) {
  QMessageBox confirmation(parent);
  confirmation.setIcon(QMessageBox::Warning);
  confirmation.setWindowTitle(AppLocale::trUi("replace_video.title"));
  confirmation.setText(AppLocale::trUi("replace_video.warning"));

  QPushButton* exportButton = confirmation.addButton(
      AppLocale::trUi("replace_video.export"), QMessageBox::AcceptRole);
  QPushButton* replaceButton = confirmation.addButton(
      AppLocale::trUi("replace_video.replace"), QMessageBox::DestructiveRole);
  QPushButton* cancelButton = confirmation.addButton(
      AppLocale::trUi("replace_video.cancel"), QMessageBox::RejectRole);
  confirmation.setDefaultButton(exportButton);
  confirmation.setEscapeButton(cancelButton);
  confirmation.exec();

  if (confirmation.clickedButton() == exportButton) {
    return ReplaceVideoChoice::ExportThenContinue;
  }
  if (confirmation.clickedButton() == replaceButton) {
    return ReplaceVideoChoice::ContinueWithoutExport;
  }
  return ReplaceVideoChoice::Cancel;
}

}  // namespace

bool replaceVideoWouldDiscardWork(const TagSession* session) {
  if (!session) {
    return false;
  }
  if (!session->tags().isEmpty()) {
    return true;
  }
  return !SessionNotesWriter::matchNotePlainText(session->matchNote()).isEmpty();
}

ReplaceVideoChoice chooseReplaceVideoAction(const TagSession* session, QWidget* parent) {
  if (!replaceVideoWouldDiscardWork(session)) {
    return ReplaceVideoChoice::ContinueWithoutExport;
  }
  return confirmReplaceVideo(parent);
}
