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
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <array>
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

bool pathIsShared(const EventLabelOverrides::FollowUpPath& path) {
  return !path.isEmpty() && EventLabelOverrides::sharedAcrossMainEvents(path.targetId());
}

QString currentLabel(TagSession* session, EventCatalog::EventId eventId,
                     const EventLabelOverrides::FollowUpPath& path) {
  if (!session) return {};
  if (pathIsShared(path)) {
    return EventLabelResolver::labelForPath(path, session->eventLabelOverrides());
  }
  return EventLabelResolver::labelForId(eventId, session);
}

class NameDialog final : public QDialog {
 public:
  NameDialog(QWidget* parent, TagSession* session, EventCatalog::EventId eventId,
             const EventLabelOverrides::FollowUpPath& path)
      : QDialog(parent), session_(session), eventId_(eventId), path_(path) {
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
    field_->setText(currentLabel(session_, eventId_, path_));
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
      if (pathIsShared(path_)) {
        const SharedScopeChoice choice = askSharedFollowUpScope(this, draft, path_);
        if (choice == SharedScopeChoice::Cancel) {
          reject();
          return;
        }
        const bool accepted = choice == SharedScopeChoice::AllInstances
                                  ? EventControlsValidation::commitLabel(&draft, eventId_, field_->text(),
                                                                         &issue)
                                  : EventControlsValidation::commitPathLabel(&draft, path_, field_->text(),
                                                                             &issue);
        if (!accepted) {
          showIssue(issue, draft);
          return;
        }
      } else if (!EventControlsValidation::commitLabel(&draft, eventId_, field_->text(), &issue)) {
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
    const EventLabelOverrides::Table& table = session_->eventLabelOverrides();
    const EventControlsValidation::Issue issue =
        pathIsShared(path_) ? EventControlsValidation::inspectPathLabel(table, path_, field_->text())
                            : EventControlsValidation::inspectLabel(table, eventId_, field_->text());
    const bool valid = issue.kind == EventControlsValidation::IssueKind::None;
    ok_->setEnabled(valid);
    error_->setText(valid ? QString()
                          : EventControlsValidation::issueMessage(issue, session_->eventLabelOverrides()));
  }

  TagSession* session_ = nullptr;
  EventCatalog::EventId eventId_ = EventCatalog::EventId::None;
  EventLabelOverrides::FollowUpPath path_;
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

void renameEvent(QWidget* parent, TagSession* session, EventCatalog::EventId eventId,
                 const EventLabelOverrides::FollowUpPath& path) {
  if (!session || EventCatalog::nodeForId(eventId) == nullptr) return;
  NameDialog dialog(parent, session, eventId, path);
  dialog.exec();
}

void changeKey(QWidget* parent, TagSession* session, EventCatalog::EventId eventId) {
  if (!session || !EventControlsValidation::isMainEvent(eventId)) return;
  KeyDialog dialog(parent, session, eventId);
  dialog.exec();
}

void setShown(QWidget* parent, TagSession* session, EventCatalog::EventId eventId,
              const EventLabelOverrides::FollowUpPath& path, bool shown) {
  if (!session) return;
  if (pathIsShared(path)) {
    const SharedScopeChoice choice =
        askSharedFollowUpScope(parent, session->eventLabelOverrides(), path);
    if (choice == SharedScopeChoice::Cancel) return;
    EventLabelOverrides::Table draft = session->eventLabelOverrides();
    EventControlsValidation::Issue issue;
    const bool accepted = choice == SharedScopeChoice::AllInstances
                              ? EventControlsValidation::commitShown(&draft, eventId, shown, &issue)
                              : EventControlsValidation::commitPathShown(&draft, path, shown, &issue);
    if (!accepted) {
      QMessageBox box(parent);
      box.setIcon(QMessageBox::Warning);
      box.setWindowTitle(AppLocale::trUi("controls_menu.show_blocked_title"));
      box.setText(EventControlsValidation::issueMessage(issue, draft));
      box.addButton(AppLocale::trUi("controls_editor.confirm_yes"), QMessageBox::AcceptRole);
      box.exec();
      return;
    }
    publishTable(session, draft);
    return;
  }
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

void resetButton(QWidget* parent, TagSession* session, EventCatalog::EventId eventId,
                const EventLabelOverrides::FollowUpPath& path) {
  if (!session) return;
  if (pathIsShared(path)) {
    const SharedScopeChoice choice =
        askSharedFollowUpScope(parent, session->eventLabelOverrides(), path);
    if (choice == SharedScopeChoice::Cancel) return;
    EventLabelOverrides::Table draft = session->eventLabelOverrides();
    if (choice == SharedScopeChoice::ThisPath) {
      draft.clearPathLabel(path);
      draft.clearPathHidden(path);
      publishTable(session, draft);
      return;
    }
  } else if (!confirmDestructive(parent, AppLocale::trUi("controls_menu.reset_title"),
                                 AppLocale::trUi("controls_menu.reset_message"))) {
    return;
  }
  EventLabelOverrides::Table draft = session->eventLabelOverrides();
  const bool wasHidden = draft.hidden(eventId);
  draft.clearLabel(eventId);
  draft.clearShortcutKey(eventId);
  draft.clearPathLabelsFor(eventId);
  draft.clearPathHiddenFor(eventId);
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

EventLabelOverrides::FollowUpPath childPath(const EventLabelOverrides::FollowUpPath& parent,
                                             EventCatalog::EventId childId) {
  EventLabelOverrides::FollowUpPath path = parent;
  if (path.stepCount < 0) path.stepCount = 0;
  if (path.stepCount >= EventCatalog::kMaxFollowUpDepth) return path;
  path.steps[static_cast<std::size_t>(path.stepCount)] = childId;
  ++path.stepCount;
  return path;
}

void addFollowUpMenu(QMenu* parentMenu, QWidget* anchor, TagSession* session,
                     const EventLabelOverrides::FollowUpPath& path) {
  const EventCatalog::EventId eventId = path.targetId();
  const EventCatalog::Node* node = EventCatalog::nodeForId(eventId);
  if (!parentMenu || !node || !session || path.isEmpty()) return;
  const QString fullName = EventLabelResolver::labelForPath(path, session->eventLabelOverrides());
  QMenu* menu = parentMenu->addMenu(elidedMenuTitle(parentMenu, fullName));
  menu->setToolTip(fullName);
  menu->setProperty("eventId", static_cast<int>(eventId));

  QWidget* dialogParent = anchor != nullptr ? anchor : static_cast<QWidget*>(menu);
  QAction* renameAction = menu->addAction(AppLocale::trUi("controls_menu.rename"));
  renameAction->setObjectName(QStringLiteral("ControlsMenuFollowUpRename"));
  renameAction->setProperty("eventId", static_cast<int>(eventId));
  QObject::connect(renameAction, &QAction::triggered, menu, [dialogParent, session, eventId, path]() {
    renameEvent(dialogParent, session, eventId, path);
  });

  const bool hidden = session->eventLabelOverrides().hiddenOnPath(path);
  QAction* visibilityAction = menu->addAction(
      AppLocale::trUi(hidden ? "controls_menu.show" : "controls_menu.hide"));
  visibilityAction->setObjectName(QStringLiteral("ControlsMenuFollowUpVisibility"));
  visibilityAction->setProperty("eventId", static_cast<int>(eventId));
  QObject::connect(visibilityAction, &QAction::triggered, menu,
                   [dialogParent, session, eventId, path, hidden]() {
                     setShown(dialogParent, session, eventId, path, hidden);
                   });

  if (node->childCount <= 0) return;
  menu->addSeparator();
  for (int childIndex = 0; childIndex < node->childCount; ++childIndex) {
    addFollowUpMenu(menu, anchor, session,
                    childPath(path, node->children[static_cast<std::size_t>(childIndex)]));
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
              const std::function<void()>& openEditor,
              const EventLabelOverrides::FollowUpPath& path) {
  if (!menu || !session || EventCatalog::nodeForId(eventId) == nullptr) return;
  menu->setObjectName(QStringLiteral("GameControlsContextMenu"));

  QWidget* anchor = menu->parentWidget();
  const EventLabelOverrides::Table& table = session->eventLabelOverrides();
  const bool hidden = path.isEmpty() ? table.hidden(eventId) : table.hiddenOnPath(path);

  QAction* renameAction = menu->addAction(AppLocale::trUi("controls_menu.rename"));
  renameAction->setObjectName(QStringLiteral("ControlsMenuRename"));
  QObject::connect(renameAction, &QAction::triggered, menu, [anchor, session, eventId, path]() {
    renameEvent(anchor, session, eventId, path);
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
  QObject::connect(visibilityAction, &QAction::triggered, menu,
                   [anchor, session, eventId, path, hidden]() {
                     setShown(anchor, session, eventId, path, hidden);
                   });

  const EventCatalog::Node* node = EventCatalog::nodeForId(eventId);
  if (node != nullptr && node->childCount > 0) {
    QMenu* followUps = menu->addMenu(AppLocale::trUi("controls_menu.follow_ups"));
    followUps->setObjectName(QStringLiteral("ControlsMenuFollowUps"));
    EventLabelOverrides::FollowUpPath root = path;
    if (root.isEmpty()) root.mainEventId = eventId;
    for (int childIndex = 0; childIndex < node->childCount; ++childIndex) {
      addFollowUpMenu(followUps, anchor, session,
                      childPath(root, node->children[static_cast<std::size_t>(childIndex)]));
    }
  }

  menu->addSeparator();
  QAction* resetAction = menu->addAction(AppLocale::trUi("controls_menu.reset"));
  resetAction->setObjectName(QStringLiteral("ControlsMenuReset"));
  QObject::connect(resetAction, &QAction::triggered, menu, [anchor, session, eventId, path]() {
    resetButton(anchor, session, eventId, path);
  });

  QAction* editAllAction = menu->addAction(AppLocale::trUi("controls_menu.edit_all"));
  editAllAction->setObjectName(QStringLiteral("ControlsMenuEditAll"));
  QObject::connect(editAllAction, &QAction::triggered, menu, [openEditor]() {
    if (openEditor) openEditor();
  });
}

void popup(QWidget* anchor, const QPoint& globalPosition, TagSession* session,
           EventCatalog::EventId eventId, const std::function<void()>& openEditor,
           const EventLabelOverrides::FollowUpPath& path) {
  if (!anchor || !session) return;
  // Stack menu: a parented unique_ptr would not delete it after exec returns.
  QMenu menu(anchor);
  populate(&menu, session, eventId, openEditor, path);
  menu.exec(globalPosition);
}

namespace {

QString readablePath(const EventLabelOverrides::Table& table,
                     const EventLabelOverrides::FollowUpPath& path) {
  QStringList parts;
  parts.append(EventLabelResolver::labelForId(path.mainEventId, table));
  EventLabelOverrides::FollowUpPath prefix;
  prefix.mainEventId = path.mainEventId;
  const int count = path.stepCount < 0 ? 0 : path.stepCount;
  for (int index = 0; index < count && index < EventCatalog::kMaxFollowUpDepth; ++index) {
    prefix.steps[static_cast<std::size_t>(index)] = path.steps[static_cast<std::size_t>(index)];
    prefix.stepCount = index + 1;
    parts.append(EventLabelResolver::labelForPath(prefix, table));
  }
  return parts.join(AppLocale::trUi("controls_shared.path_joiner"));
}

QString joinPathParts(const QStringList& parts) {
  return parts.join(AppLocale::trUi("controls_shared.path_joiner"));
}

QStringList worstCasePaths() {
  const QStringList rows = {
      joinPathParts({QStringLiteral("Aleksandra Wiśniewska-Kowalczyk"),
                     QStringLiteral("Konstantin Oberhauser-Wettstein"),
                     QStringLiteral("Đặng Thị Ngọc Hân")}),
      joinPathParts({QStringLiteral("J"), QStringLiteral("Goal")}),
      joinPathParts({QStringLiteral("Benachrichtigungseinstellungen"),
                     QStringLiteral("customer-feedback-from-enterprise-onboarding"),
                     QStringLiteral("Goal")}),
      joinPathParts({QStringLiteral("María José de la Cruz y Fernández"), QStringLiteral("PC"),
                     QStringLiteral("Variant"), QStringLiteral("Goal")}),
      joinPathParts({QStringLiteral("<script>alert(1)</script>"), QStringLiteral("&amp;"),
                     QStringLiteral("**bold**")}),
      joinPathParts({QStringLiteral("🦊 Fox"), QStringLiteral("Goal")}),
      joinPathParts({QStringLiteral("Jo"), QStringLiteral("On target"), QStringLiteral("Goal")}),
      joinPathParts({QStringLiteral("نور الهدى عبد الرحمن"), QStringLiteral("Goal")}),
  };
  return rows;
}

class SharedPathsDialog final : public QDialog {
 public:
  SharedPathsDialog(QWidget* parent, const QStringList& demoLines)
      : QDialog(parent), demoLines_(demoLines) {
    setObjectName(QStringLiteral("ControlsSharedPaths"));
    setWindowTitle(AppLocale::trUi("controls_shared.title"));
    setModal(true);
    setMinimumWidth(320);
    setMaximumWidth(640);
    resize(480, 460);

    auto layout = makeQtPtr<QVBoxLayout>(this);
    layout->setSpacing(8);
    layout->setContentsMargins(16, 16, 16, 16);

    auto intro = makeQtPtr<QLabel>(AppLocale::trUi("controls_shared.message"), this);
    intro->setObjectName(QStringLiteral("ControlsSharedIntro"));
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::PlainText);
    intro->setMinimumWidth(0);
    layout->addWidget(intro.get());

    auto empty = makeQtPtr<QLabel>(AppLocale::trUi("controls_shared.empty"), this);
    empty_ = empty.get();
    empty_->setObjectName(QStringLiteral("ControlsSharedEmpty"));
    empty_->setWordWrap(true);
    empty_->setTextFormat(Qt::PlainText);
    empty_->setMinimumWidth(0);
    empty_->setVisible(false);
    layout->addWidget(empty.get());

    auto list = makeQtPtr<QListWidget>(this);
    list_ = list.get();
    list_->setObjectName(QStringLiteral("ControlsSharedList"));
    list_->setWordWrap(true);
    list_->setTextElideMode(Qt::ElideNone);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setUniformItemSizes(false);
    list_->setMinimumWidth(0);
    list_->setMinimumHeight(120);
    list_->setMaximumHeight(240);
    list_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    list_->setSpacing(2);
    layout->addWidget(list.get(), 1);

    auto applyAll = makeQtPtr<QPushButton>(AppLocale::trUi("controls_shared.apply_all"), this);
    applyAll->setObjectName(QStringLiteral("ControlsSharedApplyAll"));
    applyAll->setMinimumWidth(0);
    applyAll->setAutoDefault(false);
    applyAll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto applyOne = makeQtPtr<QPushButton>(AppLocale::trUi("controls_shared.apply_one"), this);
    applyOne->setObjectName(QStringLiteral("ControlsSharedApplyOne"));
    applyOne->setMinimumWidth(0);
    applyOne->setAutoDefault(false);
    applyOne->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto cancel = makeQtPtr<QPushButton>(AppLocale::trUi("controls_editor.cancel"), this);
    cancel->setObjectName(QStringLiteral("ControlsSharedCancel"));
    cancel->setMinimumWidth(0);
    cancel->setAutoDefault(false);
    cancel->setDefault(true);
    cancel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(applyAll.get());
    layout->addWidget(applyOne.get());
    layout->addWidget(cancel.get());

    connect(applyAll.get(), &QPushButton::clicked, this, [this]() {
      choice_ = SharedScopeChoice::AllInstances;
      accept();
    });
    connect(applyOne.get(), &QPushButton::clicked, this, [this]() {
      choice_ = SharedScopeChoice::ThisPath;
      accept();
    });
    connect(cancel.get(), &QPushButton::clicked, this, &QDialog::reject);

    if (qEnvironmentVariableIsSet("AVA_BREAK_UI")) buildBreakToggle(layout.get());
    showLines(demoLines_);
  }

  SharedScopeChoice choice() const { return choice_; }

 protected:
  void resizeEvent(QResizeEvent* event) override {
    QDialog::resizeEvent(event);
    if (list_ != nullptr) list_->doItemsLayout();
  }

 private:
  enum class Fixture { Demo, Worst, Empty, One };

  void buildBreakToggle(QVBoxLayout* layout) {
    auto toggle = makeQtPtr<QWidget>(this);
    toggle->setObjectName(QStringLiteral("ControlsSharedBreakToggle"));
    Style::setRole(toggle.get(), "breakUiToggle");
    auto grid = makeQtPtr<QGridLayout>(toggle.get());
    grid->setContentsMargins(4, 4, 4, 4);
    grid->setSpacing(2);
    const std::array<std::pair<const char*, Fixture>, 4> segments = {{
        {"ControlsSharedBreakDemo", Fixture::Demo},
        {"ControlsSharedBreakWorst", Fixture::Worst},
        {"ControlsSharedBreakEmpty", Fixture::Empty},
        {"ControlsSharedBreakOne", Fixture::One},
    }};
    const std::array<const char*, 4> labels = {
        "controls_editor.break_demo",
        "controls_editor.break_worst",
        "controls_editor.break_empty",
        "controls_editor.break_one",
    };
    for (int index = 0; index < 4; ++index) {
      auto button = makeQtPtr<QPushButton>(AppLocale::trUi(labels[static_cast<std::size_t>(index)]),
                                           toggle.get());
      button->setObjectName(QString::fromUtf8(segments[static_cast<std::size_t>(index)].first));
      button->setCheckable(true);
      button->setMinimumWidth(0);
      button->setAutoDefault(false);
      Style::setRole(button.get(), "breakUiSegment");
      button->setChecked(index == 0);
      const Fixture fixture = segments[static_cast<std::size_t>(index)].second;
      connect(button.get(), &QPushButton::clicked, this, [this, fixture]() { applyFixture(fixture); });
      grid->addWidget(button.get(), index / 2, index % 2);
      breakButtons_.append(button.get());
    }
    layout->addWidget(toggle.get());
  }

  void applyFixture(Fixture fixture) {
    for (int index = 0; index < breakButtons_.size(); ++index) {
      if (breakButtons_.at(index) == nullptr) continue;
      breakButtons_.at(index)->setChecked(static_cast<int>(fixture) == index);
    }
    if (fixture == Fixture::Worst) showLines(worstCasePaths());
    else if (fixture == Fixture::Empty) showLines({});
    else if (fixture == Fixture::One) {
      showLines({joinPathParts({QStringLiteral("Jo"), QStringLiteral("J")})});
    } else {
      showLines(demoLines_);
    }
  }

  void showLines(const QStringList& lines) {
    if (list_ == nullptr) return;
    list_->clear();
    for (const QString& line : lines) {
      auto* item = new QListWidgetItem(line, list_);
      item->setToolTip(line);
      item->setFlags(Qt::ItemIsEnabled);
    }
    const bool empty = lines.isEmpty();
    list_->setVisible(!empty);
    if (empty_ != nullptr) empty_->setVisible(empty);
  }

  QStringList demoLines_;
  SharedScopeChoice choice_ = SharedScopeChoice::Cancel;
  QListWidget* list_ = nullptr;
  QLabel* empty_ = nullptr;
  QVector<QPushButton*> breakButtons_;
};

}  // namespace

SharedScopeChoice askSharedFollowUpScope(QWidget* parent, const EventLabelOverrides::Table& table,
                                         const EventLabelOverrides::FollowUpPath& path) {
  if (path.isEmpty() || !EventLabelOverrides::sharedAcrossMainEvents(path.targetId())) {
    return SharedScopeChoice::AllInstances;
  }
  QStringList lines;
  const QVector<EventLabelOverrides::FollowUpPath> paths =
      EventLabelOverrides::catalogPathsEndingAt(path.targetId());
  for (const EventLabelOverrides::FollowUpPath& candidate : paths) {
    if (candidate == path) continue;
    lines.append(readablePath(table, candidate));
  }
  if (lines.isEmpty()) return SharedScopeChoice::AllInstances;
  SharedPathsDialog dialog(parent, lines);
  if (dialog.exec() != QDialog::Accepted) return SharedScopeChoice::Cancel;
  return dialog.choice();
}

}  // namespace GameControlsContextMenu
