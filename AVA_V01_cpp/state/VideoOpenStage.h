#pragma once

/// Open / prepare / replace / close for the work-window video.
///
/// WorkWindow::commitVideoStage is the only writer of the stage and of the
/// source paths that belong to it. Callers pass an action; reduce decides
/// whether that action is legal and what the next stage is.
namespace VideoOpenStage {

enum class Stage {
    NoVideo,
    /// First open is in flight (team setup, transcode, or concat). No committed video.
    Preparing,
    VideoOpen,
    /// An open video is being replaced. The committed video stays until CommitOpened.
    Replacing,
};

enum class Action {
    BeginPreparation,
    BeginReplace,
    CommitOpened,
    Close,
    /// Failed open, and the user kept a running export. Preparing returns to
    /// NoVideo. Replacing returns to the committed video.
    AbandonFailedOpen,
};

struct Result {
    bool accepted = false;
    Stage stage = Stage::NoVideo;
};

inline bool allowsXmlImportAndExport(Stage stage) {
    return stage == Stage::VideoOpen;
}

inline const char* stageName(Stage stage) {
    switch (stage) {
    case Stage::NoVideo:
        return "NoVideo";
    case Stage::Preparing:
        return "Preparing";
    case Stage::VideoOpen:
        return "VideoOpen";
    case Stage::Replacing:
        return "Replacing";
    }
    return "Invalid";
}

inline const char* actionName(Action action) {
    switch (action) {
    case Action::BeginPreparation:
        return "BeginPreparation";
    case Action::BeginReplace:
        return "BeginReplace";
    case Action::CommitOpened:
        return "CommitOpened";
    case Action::Close:
        return "Close";
    case Action::AbandonFailedOpen:
        return "AbandonFailedOpen";
    }
    return "Invalid";
}

/// Refuses a transition by leaving \p stage unchanged and reporting accepted = false.
inline Result reduce(Stage stage, Action action) {
    Result refused;
    refused.accepted = false;
    refused.stage = stage;

    switch (stage) {
    case Stage::NoVideo:
        switch (action) {
        case Action::BeginPreparation:
            return Result{true, Stage::Preparing};
        case Action::Close:
            return Result{true, Stage::NoVideo};
        case Action::BeginReplace:
        case Action::CommitOpened:
        case Action::AbandonFailedOpen:
            return refused;
        }
        break;
    case Stage::Preparing:
        switch (action) {
        case Action::BeginPreparation:
            return Result{true, Stage::Preparing};
        case Action::CommitOpened:
            return Result{true, Stage::VideoOpen};
        case Action::Close:
        case Action::AbandonFailedOpen:
            return Result{true, Stage::NoVideo};
        case Action::BeginReplace:
            return refused;
        }
        break;
    case Stage::VideoOpen:
        switch (action) {
        case Action::BeginReplace:
            return Result{true, Stage::Replacing};
        case Action::Close:
            return Result{true, Stage::NoVideo};
        case Action::BeginPreparation:
        case Action::CommitOpened:
        case Action::AbandonFailedOpen:
            return refused;
        }
        break;
    case Stage::Replacing:
        switch (action) {
        case Action::BeginReplace:
            return Result{true, Stage::Replacing};
        case Action::CommitOpened:
        case Action::AbandonFailedOpen:
            return Result{true, Stage::VideoOpen};
        case Action::Close:
            return Result{true, Stage::NoVideo};
        case Action::BeginPreparation:
            return refused;
        }
        break;
    }
    return refused;
}

}  // namespace VideoOpenStage
