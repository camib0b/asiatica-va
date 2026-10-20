#pragma once

#include <QDialog>
#include <QPointer>
#include <QStringList>
#include <QVector>

#include "VideoClipSequence.h"

class QFrame;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QWidget;

class ConcatFileOrderDialog final : public QDialog {
    Q_OBJECT

public:
    explicit ConcatFileOrderDialog(const QStringList& filePaths, QWidget* parent = nullptr);

    QStringList orderedFilePaths() const;

private:
    void buildUi();
    void applyStaticCopy();
    void refreshSummary();
    void rebuildRows();
    void clearRows();
    QFrame* createRow(int index);
    void queueRebuild();
    void flushRebuild();
    void revealSelectedRow();
    void moveRow(int fromIndex, int toIndex);
    void resetChronologicalOrder();
    void selectRow(int index);
    int indexForKeyboard() const;
    void beginDrag();
    void dragOver(int fromIndex, int canvasY);
    void dropAt(int fromIndex, int canvasY);
    void finishDrag();
    void hideDropIndicator();
    int insertBeforeForCanvasY(int canvasY) const;
    void applyFilePaths(const QStringList& filePaths);
    int indexOfFilePath(const QString& filePath) const;

    QStringList originalFilePaths_;
    QVector<VideoClip> clips_;
    int selectedIndex_ = -1;
    bool rebuildQueued_ = false;
    bool rebuildNeeded_ = false;
    bool insideDrag_ = false;

    QPointer<QLabel> headlineLabel_;
    QPointer<QLabel> subtitleLabel_;
    QPointer<QScrollArea> scrollArea_;
    QPointer<QWidget> listCanvas_;
    QPointer<QVBoxLayout> listLayout_;
    QPointer<QFrame> dropIndicator_;
    QPointer<QLabel> flowLabel_;
    QPointer<QLabel> outputTitleLabel_;
    QPointer<QLabel> outputDetailLabel_;
    QPointer<QPushButton> resetButton_;
    QPointer<QPushButton> cancelButton_;
    QPointer<QPushButton> continueButton_;
    QPointer<QWidget> fixtureBar_;
    QVector<QPointer<QFrame>> rows_;
};
