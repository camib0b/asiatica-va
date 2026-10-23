#include "GameControlsContextMenu.h"

#include "QtPtr.h"

#include "../i18n/AppLocale.h"
#include "../state/EventCatalog.h"
#include "../state/EventControlsValidation.h"
#include "../state/EventLabelResolver.h"
#include "../state/TagSession.h"
#include "../style/StyleProps.h"

#include <QAction>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <functional>

namespace GameControlsContextMenu {
namespace {

constexpr int kMenuTitleWidthPx = 280;

QString elidedMenuTitle(const QMenu* menu, const QString& fullText) {
  if (!menu) return fullText;
  return menu->fontMetrics().elidedText(fullText, Qt::ElideMiddle, kMenuTitleWidthPx);
}

void publishTable(TagSession* session, const EventLabelOverrides::Table& table) {
  if (!session) return;
  session->replaceEventLabelOverrides(table);
}

class NameDialog final : public QDialog {
 public:
  NameDialog(QWidget* parent, TagSession* session, EventCatalog::EventId eventId)
      : QDialog(parent), session_(session), eventId_(eventId) {
    setObjectName(QStringLiteral("ControlsRenameDialog"));
    setWindowTitle(AppLocale::trUi("controls_rename.title"));
    setModal(true);

    auto layout = makeQtPtr<QVBoxLayout>(this);
    layout->setSpacing(8);
    layout->setContentsMargins(16, 16, 16, 16);

    auto prompt = makeQtPtr<QLabel>(AppLocale::trUi("controls_rename.prompt"), this);
    prompt->setTextFormat(Qt::PlainText);
    layout->addWidget(prompt.get());

    auto field = makeQtPtr<QLineEdit>(this);
    field_ = field.get();
    field_->setObjectName(QStringLiteral("ControlsRenameField"));
    field_->setText(EventLabelResolver::labelForId(eventId_, session_));
    field_->setClearButtonEnabled(true);
    field_->setMinimumWidth(0);
    layout->addWidget(field.get());

    auto error = makeQtPtr<QLabel>(this);
    error_ = error.get();
    error_->setObjectName(QStringLiteral("ControlsRenameError"));
    error_->setWordWrap(true);
    error_->setTextFormat(Qt::PlainText);
    Style::setRole(error_, "inlineError");
    layout->addWidget(error.get());

    auto buttons = makeQtPtr<QHBoxLayout>();
    buttons->addStretch(1);
    auto cancel = makeQtPtr<QPushButton>(AppLocale::trUi("controls_editor.cancel"), this);
    cancel->setObjectName(QStringLiteral("ControlsRenameCancel"));
    auto ok = makeQtPtr<QPushButton>(AppLocale::trUi("controls_rename.ok"), this);
    ok->setObjectName(QStringLiteral("ControlsRenameOk"));
    ok_ = ok.get();
    buttons->addWidget(cancel.get());
    buttons->addWidget(ok.get());
    layout->addLayout(buttons.get());

    connect(cancel.get(), &QPushButton::clicked, this, &QDialog::reject);
    connect(ok.get(), &QPushButton::clicked, this, [this]() {
      if (!session_ || !ok_->isEnabled()) return;
      EventLabelOverrides::Table draft = session_->eventLabelOverrides();
      EventControlsValidation::Issue issue;
      if (!EventControlsValidation::commitLabel(&draft, eventId_, field_->text(), &issue)) {
        showIssue(issue, draft);
        return;
      }
      publishTable(session_, draft);
      accept();
    });
    connect(field_, &QLineEdit::textChanged, this, [this](const QString&) { refresh(); });
    connect(field_, &QLineEdit::returnPressed, ok_, &QPushButton::click);
    refresh();
    field_->selectAll();
  }

 private:
  void showIssue(const EventControlsValidation::Issue& issue,
                 const EventLabelOverrides::Table& table) {
    error_->setText(EventControlsValidation::issueMessage(issue, table));
  }

  void refresh() {
    if (!session_) {
      ok_->setEnabled(false);
      return;
    }
    const EventControlsValidation::Issue issue = EventControlsValidation::inspectLabel(
        session_->eventLabelOverrides(), eventId_, field_->text());
    const bool valid = issue.kind == EventControlsValidation::IssueKind::None;
    ok_->setEnabled(valid);
    error_->setText(valid ? QString()
                          : EventControlsValidation::issueMessage(issue, session_->eventLabelOverrides()));
  }

  TagSession* session_ = nullptr;
  EventCatalog::EventId eventId_ = EventCatalog::EventId::None;
  QLineEdit* field_ = nullptr;
  QLabel* error_ = nullptr;
  QPushButton* ok_ = nullptr;
};

class KeyDialog final : public QDialog {
 public:
  KeyDialog(QWidget* parent, TagSession* session, EventCatalog::EventId eventId)
      : QDialog(parent), session_(session), eventId_(eventId) {
    setObjectName(QStringLiteral("ControlsKeyDialog"));
    setWindowTitle(AppLocale::trUi("controls_key.title"));
    setModal(true);

    auto layout = makeQtPtr<QVBoxLayout>(this);
    layout->setSpacing(8);
    layout->setContentsMargins(16, 16, 16, 16);

    auto prompt = makeQtPtr<QLabel>(AppLocale::trUi("controls_key.prompt"), this);
    prompt->setTextFormat(Qt::PlainText);
    layout->addWidget(prompt.get());

    auto field = makeQtPtr<QLineEdit>(this);
    field_ = field.get();
    field_->setObjectName(QStringLiteral("ControlsKeyField"));
    field_->setText(EventControlsValidation::letterForKey(
        EventControlsValidation::effectiveShortcut(eventId_, session_->eventLabelOverrides())));
    field_->setMinimumWidth(0);
    layout->addWidget(field.get());

    auto error = makeQtPtr<QLabel>(this);
    error_ = error.get();
    error_->setObjectName(QStringLiteral("ControlsKeyError"));
    error_->setWordWrap(true);
    error_->setTextFormat(Qt::PlainText);
    Style::setRole(error_, "inlineError");
    layout->addWidget(error.get());

    auto buttons = makeQtPtr<QHBoxLayout>();
    buttons->addStretch(1);
    auto cancel = makeQtPtr<QPushButton>(AppLocale::trUi("controls_editor.cancel"), this);
    cancel->setObjectName(QStringLiteral("ControlsKeyCancel"));
    auto ok = makeQtPtr<QPushButton>(AppLocale::trUi("controls_key.ok"), this);
    ok->setObjectName(QStringLiteral("ControlsKeyOk"));
    ok_ = ok.get();
    buttons->addWidget(cancel.get());
    buttons->addWidget(ok.get());
    layout->addLayout(buttons.get());

    connect(cancel.get(), &QPushButton::clicked, this, &QDialog::reject);
    connect(ok.get(), &QPushButton::clicked, this, [this]() {
      if (!session_ || !ok_->isEnabled()) return;
      EventLabelOverrides::Table draft = session_->eventLabelOverrides();
      EventControlsValidation::Issue issue;
      if (!EventControlsValidation::commitShortcutText(&draft, eventId_, field_->text(), &issue)) {
        error_->setText(EventControlsValidation::issueMessage(issue, draft));
        return;
      }
      publishTable(session_, draft);
      accept();
    });
    connect(field_, &QLineEdit::textChanged, this, [this](const QString&) { refresh(); });
    connect(field_, &QLineEdit::returnPressed, ok_, &QPushButton::click);
    refresh();
    field_->selectAll();
  }

 private:
  void refresh() {
    if (!session_) {
      ok_->setEnabled(false);
      return;
    }
    const EventControlsValidation::Issue issue = EventControlsValidation::inspectShortcutText(
        session_->eventLabelOverrides(), eventId_, field_->text());
    const bool valid = issue.kind == EventControlsValidation::IssueKind::None;
    ok_->setEnabled(valid);
    error_->setText(valid ? QString()
                          : EventControlsValidation::issueMessage(
                                issue, session_->eventLabelOverrides()));
  }

  TagSession* session_ = nullptr;
  EventCatalog::EventId eventId_ = EventCatalog::EventId::None;
  QLineEdit* field_ = nullptr;
  QLabel* error_ = nullptr;
  QPushButton* ok_ = nullptr;
};

void renameEvent(QWidget* parent, TagSession* session, EventCatalog::EventId eventId) {
  if (!session || EventCatalog::nodeForId(eventId) == nullptr) return;
  NameDialog dialog(parent, session, eventId);
  dialog.exec();
}

void changeKey(QWidget* parent, TagSession* session, EventCatalog::EventId eventId) {
  if (!session || !EventControlsValidation::isMainEvent(eventId)) return;
  KeyDialog dialog(parent, session, eventId);
  dialog.exec();
}

void setShown(QWidget* parent, TagSession* session, EventCatalog::EventId eventId, bool shown) {
  if (!session) return;
  if (!shown) {
    const bool followUp = !EventControlsValidation::isMainEvent(eventId);
    const QString title = AppLocale::trUi(followUp ? "controls_menu.hide_follow_up_title"
                                                   : "controls_menu.hide_title");
    const QString message = AppLocale::trUi(followUp ? "controls_menu.hide_follow_up_message"
                                                     : "controls_menu.hide_message");
    if (!confirmDestructive(parent, title, message)) return;
  }
  EventLabelOverrides::Table draft = session->eventLabelOverrides();
  EventControlsValidation::Issue issue;
  if (!EventControlsValidation::commitShown(&draft, eventId, shown, &issue)) {
    QMessageBox box(parent);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(AppLocale::trUi("controls_menu.show_blocked_title"));
    box.setText(EventControlsValidation::issueMessage(issue, draft));
    box.addButton(AppLocale::trUi("controls_editor.confirm_yes"), QMessageBox::AcceptRole);
    box.exec();
    return;
  }
  publishTable(session, draft);
}

void resetButton(QWidget* parent, TagSession* session, EventCatalog::EventId eventId) {
  if (!session) return;
  if (!confirmDestructive(parent, AppLocale::trUi("controls_menu.reset_title"),
                          AppLocale::trUi("controls_menu.reset_message"))) {
    return;
  }
  EventLabelOverrides::Table draft = session->eventLabelOverrides();
  const bool wasHidden = draft.hidden(eventId);
  draft.clearLabel(eventId);
  draft.clearShortcutKey(eventId);
  if (wasHidden) {
    EventControlsValidation::Issue issue;
    if (!EventControlsValidation::commitShown(&draft, eventId, true, &issue)) {
      QMessageBox box(parent);
      box.setIcon(QMessageBox::Warning);
      box.setWindowTitle(AppLocale::trUi("controls_menu.show_blocked_title"));
      box.setText(EventControlsValidation::issueMessage(issue, draft));
      box.addButton(AppLocale::trUi("controls_editor.confirm_yes"), QMessageBox::AcceptRole);
      box.exec();
      return;
    }
  }
  publishTable(session, draft);
}

void addFollowUpMenu(QMenu* parentMenu, QWidget* anchor, TagSession* session,
                     EventCatalog::EventId eventId) {
  const EventCatalog::Node* node = EventCatalog::nodeForId(eventId);
  if (!parentMenu || !node || !session) return;
  const QString fullName = EventLabelResolver::labelForId(eventId, session);
  QMenu* menu = parentMenu->addMenu(elidedMenuTitle(parentMenu, fullName));
  menu->setToolTip(fullName);
  menu->setProperty("eventId", static_cast<int>(eventId));

  QWidget* dialogParent = anchor != nullptr ? anchor : static_cast<QWidget*>(menu);
  QAction* renameAction = menu->addAction(AppLocale::trUi("controls_menu.rename"));
  renameAction->setObjectName(QStringLiteral("ControlsMenuFollowUpRename"));
  renameAction->setProperty("eventId", static_cast<int>(eventId));
  QObject::connect(renameAction, &QAction::triggered, menu, [dialogParent, session, eventId]() {
    renameEvent(dialogParent, session, eventId);
  });

  const bool hidden = session->eventLabelOverrides().hidden(eventId);
  QAction* visibilityAction = menu->addAction(
      AppLocale::trUi(hidden ? "controls_menu.show" : "controls_menu.hide"));
  visibilityAction->setObjectName(QStringLiteral("ControlsMenuFollowUpVisibility"));
  visibilityAction->setProperty("eventId", static_cast<int>(eventId));
  QObject::connect(visibilityAction, &QAction::triggered, menu,
                   [dialogParent, session, eventId, hidden]() {
                     setShown(dialogParent, session, eventId, hidden);
                   });

  if (node->childCount <= 0) return;
  menu->addSeparator();
  for (int childIndex = 0; childIndex < node->childCount; ++childIndex) {
    addFollowUpMenu(menu, anchor, session, node->children[static_cast<std::size_t>(childIndex)]);
  }
}

}  // namespace

bool confirmDestructive(QWidget* parent, const QString& title, const QString& message) {
  QMessageBox box(parent);
  box.setObjectName(QStringLiteral("ControlsConfirm"));
  box.setIcon(QMessageBox::Warning);
  box.setWindowTitle(title);
  box.setText(message);
  QPushButton* yes =
      box.addButton(AppLocale::trUi("controls_editor.confirm_yes"), QMessageBox::YesRole);
  yes->setObjectName(QStringLiteral("ControlsConfirmYes"));
  QPushButton* no =
      box.addButton(AppLocale::trUi("controls_editor.confirm_no"), QMessageBox::NoRole);
  no->setObjectName(QStringLiteral("ControlsConfirmNo"));
  box.setDefaultButton(no);
  box.exec();
  return box.clickedButton() == yes;
}

void populate(QMenu* menu, TagSession* session, EventCatalog::EventId eventId,
              const std::function<void()>& openEditor) {
  if (!menu || !session || EventCatalog::nodeForId(eventId) == nullptr) return;
  menu->setObjectName(QStringLiteral("GameControlsContextMenu"));

  QWidget* anchor = menu->parentWidget();
  const EventLabelOverrides::Table& table = session->eventLabelOverrides();
  const bool hidden = table.hidden(eventId);

  QAction* renameAction = menu->addAction(AppLocale::trUi("controls_menu.rename"));
  renameAction->setObjectName(QStringLiteral("ControlsMenuRename"));
  QObject::connect(renameAction, &QAction::triggered, menu, [anchor, session, eventId]() {
    renameEvent(anchor, session, eventId);
  });

  if (EventControlsValidation::isMainEvent(eventId)) {
    QAction* keyAction = menu->addAction(AppLocale::trUi("controls_menu.change_key"));
    keyAction->setObjectName(QStringLiteral("ControlsMenuChangeKey"));
    QObject::connect(keyAction, &QAction::triggered, menu, [anchor, session, eventId]() {
      changeKey(anchor, session, eventId);
    });
  }

  QAction* visibilityAction =
      menu->addAction(AppLocale::trUi(hidden ? "controls_menu.show" : "controls_menu.hide"));
  visibilityAction->setObjectName(QStringLiteral("ControlsMenuVisibility"));
  QObject::connect(visibilityAction, &QAction::triggered, menu, [anchor, session, eventId, hidden]() {
    setShown(anchor, session, eventId, hidden);
  });

  const EventCatalog::Node* node = EventCatalog::nodeForId(eventId);
  if (node != nullptr && node->childCount > 0) {
    QMenu* followUps = menu->addMenu(AppLocale::trUi("controls_menu.follow_ups"));
    followUps->setObjectName(QStringLiteral("ControlsMenuFollowUps"));
    for (int childIndex = 0; childIndex < node->childCount; ++childIndex) {
      addFollowUpMenu(followUps, anchor, session,
                      node->children[static_cast<std::size_t>(childIndex)]);
    }
  }

  menu->addSeparator();
  QAction* resetAction = menu->addAction(AppLocale::trUi("controls_menu.reset"));
  resetAction->setObjectName(QStringLiteral("ControlsMenuReset"));
  QObject::connect(resetAction, &QAction::triggered, menu, [anchor, session, eventId]() {
    resetButton(anchor, session, eventId);
  });

  QAction* editAllAction = menu->addAction(AppLocale::trUi("controls_menu.edit_all"));
  editAllAction->setObjectName(QStringLiteral("ControlsMenuEditAll"));
  QObject::connect(editAllAction, &QAction::triggered, menu, [openEditor]() {
    if (openEditor) openEditor();
  });
}

void popup(QWidget* anchor, const QPoint& globalPosition, TagSession* session,
           EventCatalog::EventId eventId, const std::function<void()>& openEditor) {
  if (!anchor || !session) return;
  // Stack menu: a parented unique_ptr would not delete it after exec returns.
  QMenu menu(anchor);
  populate(&menu, session, eventId, openEditor);
  menu.exec(globalPosition);
}

}  // namespace GameControlsContextMenu
