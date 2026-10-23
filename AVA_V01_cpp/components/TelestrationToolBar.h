#pragma once

#include "../state/Telestration.h"

#include <QWidget>

class QPushButton;

/// Tool and ink picker drawn over the video picture.
class TelestrationToolBar final : public QWidget {
    Q_OBJECT

public:
    explicit TelestrationToolBar(QWidget* parent = nullptr);

    Telestration::Tool tool() const { return tool_; }
    Telestration::InkColor inkColor() const { return inkColor_; }
    void setActionsEnabled(bool enabled);
    void applyUiStrings();

signals:
    void toolChanged(Telestration::Tool tool);
    void inkColorChanged(Telestration::InkColor color);
    void undoRequested();

private:
    void selectTool(Telestration::Tool tool);
    void selectInkColor(Telestration::InkColor color);
    void refreshToolIcons();
    void refreshSwatchIcons();

    Telestration::Tool tool_ = Telestration::Tool::None;
    Telestration::InkColor inkColor_ = Telestration::InkColor::White;
    QPushButton* arrowButton_ = nullptr;
    QPushButton* dottedArrowButton_ = nullptr;
    QPushButton* circleButton_ = nullptr;
    QPushButton* freehandButton_ = nullptr;
    QPushButton* undoButton_ = nullptr;
    QPushButton* whiteButton_ = nullptr;
    QPushButton* blackButton_ = nullptr;
    QPushButton* blueButton_ = nullptr;
    QPushButton* redButton_ = nullptr;
};
