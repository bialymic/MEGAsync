#ifndef MEGATRANSFERVIEW_H
#define MEGATRANSFERVIEW_H

#include "TransfersWidget.h"

#include <QGraphicsEffect>
#include <QTreeView>
#include <QMenu>
#include <QMouseEvent>
#include <QFutureWatcher>

class MegaTransferView : public QTreeView
{
    Q_OBJECT

    struct SelectedIndexesInfo
    {
        QString actionText;
        bool isAnyCancellable;
        bool areAllCancellable;
        bool areAllSync;

        SelectedIndexesInfo():isAnyCancellable(false), areAllCancellable(true),areAllSync(true){}
    };

public:
    static const int CANCEL_MESSAGE_THRESHOLD;

    MegaTransferView(QWidget* parent = 0);
    void setup();
    void setup(TransfersWidget* tw);
    void enableContextMenu();

    void onPauseResumeVisibleRows(bool isPaused);
    bool onCancelAllTransfers();
    void onClearAllTransfers();
    void onCancelAndClearVisibleTransfers();
    void onClearVisibleTransfers();

    int getVerticalScrollBarWidth() const;

    SelectedIndexesInfo getVisibleCancelOrClearInfo();
    SelectedIndexesInfo getSelectedCancelOrClearInfo();

    //Static messages for messageboxes
    static QString retryAskActionText(int count);
    static QString cancelAskActionText(int count);
    static QString clearAskActionText(int count);
    static QString clearAndCancelAskActionText(int count);
    static QString pauseActionText(int count);
    static QString resumeActionText(int count);
    static QString cancelActionText(int count);
    static QString clearActionText(int count);

public slots:
    void onPauseResumeSelection(bool pauseState);
    void onCancelVisibleTransfers();
    void onCancelSelectedTransfers();
    void onRetryVisibleTransfers();
    void onCancelClearSelection(bool isClear);

signals:
    void verticalScrollBarVisibilityChanged(bool status);
    void pauseResumeTransfersByContextMenu(bool pause);

protected:
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void selectionChanged(const QItemSelection &selected, const QItemSelection &deselected) override;
    bool eventFilter(QObject *object, QEvent *event) override;

private slots:
    void onCustomContextMenu(const QPoint& point);
    void moveToTopClicked();
    void moveUpClicked();
    void moveDownClicked();
    void moveToBottomClicked();
    void getLinkClicked();
    void openInMEGAClicked();
    void openItemClicked();
    void showInFolderClicked();
    void showInMegaClicked();
    void cancelSelectedClicked();
    void clearSelectedClicked();
    void pauseSelectedClicked();
    void resumeSelectedClicked();
    void onInternalMoveStarted();
    void onInternalMoveFinished();
    void onOpenUrlFinished();

private:
    friend class TransferManagerDelegateWidget;

    bool mDisableLink;
    bool mKeyNavigation;

    TransfersWidget* mParentTransferWidget;
    QFutureWatcher<bool> mOpenUrlWatcher;

    QMenu *createContextMenu();
    void addSeparatorToContextMenu(bool& addSeparator, QMenu* contextMenu);

    void clearAllTransfers();
    void cancelAllTransfers();

    QModelIndexList getTransfers(bool onlyVisible, TransferData::TransferStates state = TransferData::TRANSFER_NONE);
    QModelIndexList getSelectedTransfers();

    void showOpeningFileError();
};

#endif // MEGATRANSFERVIEW_H
