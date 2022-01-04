#include "TransfersWidget.h"
#include "ui_TransfersWidget.h"
#include "MegaApplication.h"

#include <QTimer>
#include <QtConcurrent/QtConcurrent>

const int TransfersWidget::PROXY_ACTIVITY_TIMEOUT_MS;

TransfersWidget::TransfersWidget(QWidget* parent) :
    QWidget (parent),
    ui (new Ui::TransfersWidget),
    model (nullptr),
    model2 (nullptr),
    tDelegate (nullptr),
    tDelegate2 (nullptr),
    mIsPaused (false),
    app (qobject_cast<MegaApplication*>(qApp)),
    mHeaderNameState (SORT_DESCENDING),
    mHeaderSizeState (SORT_DESCENDING),
    mThreadPool(ThreadPoolSingleton::getInstance()),
    mProxyActivityTimer (new QTimer(this)),
    mProxyActivityMessage (new QMessageBox(this))
{
    ui->setupUi(this);
}

void TransfersWidget::setupTransfers(std::shared_ptr<mega::MegaTransferData> transferData, QTransfersModel::ModelType type)
{
    mType = type;
    model = new QActiveTransfersModel(type, transferData);

    connect(model, SIGNAL(noTransfers()), this, SLOT(noTransfers()));
    connect(model, SIGNAL(onTransferAdded()), this, SLOT(onTransferAdded()));

    configureTransferView();

    if ((type == mega::MegaTransfer::TYPE_DOWNLOAD && transferData->getNumDownloads())
            || (type == mega::MegaTransfer::TYPE_UPLOAD && transferData->getNumUploads()))
    {
        onTransferAdded();
    }
}

void TransfersWidget::setupTransfers()
{
//    auto createModelFuture (QtConcurrent::run([=]
//    {
        model2 = new QTransfersModel2(nullptr);
//    }));

    mProxyModel = new TransfersSortFilterProxyModel(this);
    mProxyModel->setDynamicSortFilter(false);

//    createModelFuture.waitForFinished();

    mProxyModel->setSourceModel(model2);

    configureTransferView();
    model2->initModel();

//    onTransferAdded();
}

void TransfersWidget::setupFinishedTransfers(QList<mega::MegaTransfer*> transferData,
                                             QTransfersModel::ModelType modelType)
{
    mType = modelType;
    model = new QFinishedTransfersModel(transferData, modelType);
    connect(model, SIGNAL(noTransfers()), this, SLOT(noTransfers()));
    connect(model, SIGNAL(onTransferAdded()), this, SLOT(onTransferAdded()));
    // Subscribe to MegaApplication for changes on finished transfers generated by other finished model to keep consistency
    connect(app, SIGNAL(clearAllFinishedTransfers()), model, SLOT(removeAllTransfers()));
    connect(app, SIGNAL(clearFinishedTransfer(int)),  model, SLOT(removeTransferByTag(int)));

    configureTransferView();

    if (transferData.size())
    {
        onTransferAdded();
    }
}

void TransfersWidget::refreshTransferItems()
{
    if (model) model->refreshTransfers();
}

TransfersWidget::~TransfersWidget()
{
    delete ui;
    if (tDelegate) delete tDelegate;
    if (tDelegate2) delete tDelegate2;
    if (model) delete model;
    if (model2) delete model2;
    if (mProxyModel) delete mProxyModel;
}

bool TransfersWidget::areTransfersActive()
{
    return model && model->rowCount(QModelIndex()) != 0;
}

void TransfersWidget::configureTransferView()
{
    if (!model && !model2)
    {
        return;
    }

    if (model)
    {
        tDelegate = new MegaTransferDelegate(model, this);
        ui->tvTransfers->setup(mType);
        ui->tvTransfers->setItemDelegate(tDelegate);
        ui->tvTransfers->setModel(model);
    }
    else
    {
        tDelegate2 = new MegaTransferDelegate2(mProxyModel, ui->tvTransfers, this);
        ui->tvTransfers->setup(this);
        ui->tvTransfers->setModel(mProxyModel);
        ui->tvTransfers->setItemDelegate(tDelegate2);
        onPauseStateChanged(model2->areAllPaused());

        mProxyActivityTimer->setSingleShot(true);
        connect(mProxyActivityTimer, &QTimer::timeout,
                mProxyActivityMessage, &QMessageBox::exec);

        connect(mProxyModel, &TransfersSortFilterProxyModel::modelAboutToBeSorted,
                this, [this]
        {
            mProxyActivityMessage->setText(tr("Sorting..."));
            mProxyActivityTimer->start(std::chrono::milliseconds(PROXY_ACTIVITY_TIMEOUT_MS));
        });

        connect(mProxyModel, &TransfersSortFilterProxyModel::modelSorted,
                this, [this]
        {
            mProxyActivityTimer->stop();
            mProxyActivityMessage->hide();
        });

        connect(mProxyModel, &TransfersSortFilterProxyModel::modelAboutToBeFiltered,
                this, [this]
        {
            mProxyActivityMessage->setText(tr("Filtering..."));
            mProxyActivityTimer->start(std::chrono::milliseconds(PROXY_ACTIVITY_TIMEOUT_MS));
        });

        connect(mProxyModel, &TransfersSortFilterProxyModel::modelFiltered,
                this, [this]
        {
            mProxyActivityTimer->stop();
            mProxyActivityMessage->hide();
        });

//        QObject::connect(this, &TransfersWidget::updateSearchFilter,
////                         mProxyModel,static_cast<void (TransfersSortFilterProxyModel::*)(const QRegularExpression&)>(&TransfersSortFilterProxyModel::setFilterRegularExpression),
//                         mProxyModel, &TransfersSortFilterProxyModel::setFilterFixedString,
//                Qt::QueuedConnection);
    }

    ui->tvTransfers->setDragEnabled(true);
    ui->tvTransfers->viewport()->setAcceptDrops(true);
    ui->tvTransfers->setDropIndicatorShown(true);
    ui->tvTransfers->setDragDropMode(QAbstractItemView::InternalMove);
}

void TransfersWidget::pausedTransfers(bool paused)
{
    mIsPaused = paused;
    if (model && model->rowCount(QModelIndex()) == 0)
    {
    }
    else
    {
        ui->sWidget->setCurrentWidget(ui->pTransfers);
    }
}

void TransfersWidget::disableGetLink(bool disable)
{
    ui->tvTransfers->disableGetLink(disable);
}

QTransfersModel *TransfersWidget::getModel()
{
    return model;
}

QTransfersModel2* TransfersWidget::getModel2()
{
    return model2;
}

void TransfersWidget::on_pHeaderName_clicked()
{
    Qt::SortOrder order (Qt::AscendingOrder);
    int column (-1);

    switch (mHeaderNameState)
    {
        case SORT_DESCENDING:
        {
            order = Qt::DescendingOrder;
            column = 0;
            break;
        }
        case SORT_ASCENDING:
        {
            order = Qt::AscendingOrder;
            column = 0;
            break;
        }
        case SORT_DEFAULT:
        case NB_STATES: //this never should happen
        {
            break;
        }
    }

    if (mHeaderSizeState != SORT_DESCENDING)
    {
        setHeaderState(ui->pHeaderSize, SORT_DEFAULT);
        mHeaderSizeState = SORT_DESCENDING;
        //    QtConcurrent::run([=]
        mThreadPool->push([=]
        {
            mProxyModel->sort(-1, order);
        });
    }

    //    QtConcurrent::run([=]
    mThreadPool->push([=]
    {
        mProxyModel->setSortBy(TransfersSortFilterProxyModel::SortCriterion::NAME);
        mProxyModel->sort(column, order);
    });

    setHeaderState(ui->pHeaderName, mHeaderNameState);
    mHeaderNameState = static_cast<HeaderState>((mHeaderNameState + 1) % NB_STATES);
}

void TransfersWidget::on_pHeaderSize_clicked()
{
    Qt::SortOrder order (Qt::AscendingOrder);
    int column (-1);

    switch (mHeaderSizeState)
    {
        case SORT_DESCENDING:
        {
            order = Qt::DescendingOrder;
            column = 0;
            break;
        }
        case SORT_ASCENDING:
        {
            order = Qt::AscendingOrder;
            column = 0;
            break;
        }
        case NB_STATES: //this never should happen
        default:
        {
            break;
        }
    }

    if (mHeaderNameState != SORT_DESCENDING)
    {
        setHeaderState(ui->pHeaderName, SORT_DEFAULT);
        mHeaderNameState = SORT_DESCENDING;
        //        //    QtConcurrent::run([=]
        mThreadPool->push([=]
        {
            mProxyModel->sort(-1, order);
        });
    }

    //    QtConcurrent::run([=]
    mThreadPool->push([=]
    {
        mProxyModel->setSortBy(TransfersSortFilterProxyModel::SortCriterion::TOTAL_SIZE);
        mProxyModel->sort(column, order);
    });

    setHeaderState(ui->pHeaderSize, mHeaderSizeState);
    mHeaderSizeState = static_cast<HeaderState>((mHeaderSizeState + 1) % NB_STATES);
}

void TransfersWidget::on_tPauseResumeAll_clicked()
{
    onPauseStateChanged(!mIsPaused);
    emit pauseResumeAllRows(mIsPaused);
}

void TransfersWidget::on_tCancelAll_clicked()
{
    emit cancelClearAllRows(true, true);
}

void TransfersWidget::onTransferAdded()
{
    ui->sWidget->setCurrentWidget(ui->pTransfers);
    ui->tvTransfers->scrollToTop();
}

void TransfersWidget::onShowCompleted(bool showCompleted)
{
    if (showCompleted)
    {
        ui->lHeaderTime->setText(tr("Time"));
        ui->tCancelAll->setToolTip(tr("Clear All"));
        ui->lHeaderSpeed->setText(tr("Avg. speed"));
    }
    else
    {
        ui->lHeaderTime->setText(tr("Time left"));
        ui->tCancelAll->setToolTip(tr("Cancel or Clear All"));
        ui->lHeaderSpeed->setText(tr("Speed"));
    }

    ui->tPauseResumeAll->setVisible(!showCompleted);
}

void TransfersWidget::onPauseStateChanged(bool pauseState)
{
    ui->tPauseResumeAll->setIcon(pauseState ?
                                     QIcon(QString::fromUtf8(":/images/lists_resume_all_ico.png"))
                                   : QIcon(QString::fromUtf8(":/images/lists_pause_all_ico.png")));
    ui->tPauseResumeAll->setToolTip(pauseState ?
                                        tr("Resume visible transfers")
                                      : tr("Pause visible transfers"));
    mIsPaused = pauseState;
}

void TransfersWidget::textFilterChanged(const QString& pattern)
{
//    QtConcurrent::run([=]
    mThreadPool->push([=]
    {
//        QMutexLocker lock (mFilterMutex);
        std::unique_ptr<mega::MegaApiLock> apiLock (app->getMegaApi()->getMegaApiLock(true));
        mProxyModel->setFilterFixedString(pattern);
    });

    ui->tvTransfers->scrollToTop();
}

void TransfersWidget::filtersChanged(const TransferData::TransferTypes transferTypes,
                                     const TransferData::TransferStates transferStates,
                                     const TransferData::FileTypes fileTypes)
{
//    mThreadPool->push([=]
//    {
        mProxyModel->setFilters(transferTypes, transferStates, fileTypes);
//    });
}

void TransfersWidget::transferFilterReset(bool invalidate)
{
//    mFilterMutex->lock();
    mThreadPool->push([=]
    {
        mProxyModel->resetAllFilters(invalidate);
//        mFilterMutex->unlock();
    });
}

void TransfersWidget::transferFilterApply(bool invalidate)
{
    if (!mProxyModel->dynamicSortFilter())
    {
        std::unique_ptr<mega::MegaApiLock> apiLock (app->getMegaApi()->getMegaApiLock(true));
        mProxyModel->applyFilters(false);
        mProxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
        mProxyModel->setDynamicSortFilter(true);
    }
    else
    {
        mThreadPool->push([=]
        {
            std::unique_ptr<mega::MegaApiLock> apiLock (app->getMegaApi()->getMegaApiLock(true));
            mProxyModel->resetNumberOfItems();
            mProxyModel->applyFilters(invalidate);
        });
    }
    ui->tvTransfers->scrollToTop();
}

int TransfersWidget::rowCount()
{
    return ui->tvTransfers->model()->rowCount();
}

void TransfersWidget::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        ui->retranslateUi(this);
    }
    QWidget::changeEvent(event);
}

void TransfersWidget::setHeaderState(QPushButton* header, HeaderState state)
{
    QIcon icon;
    switch (state)
    {
        case SORT_DESCENDING:
        {
            icon = Utilities::getCachedPixmap(QLatin1Literal(":/images/sort_descending.png"));
            break;
        }
        case SORT_ASCENDING:
        {
            icon = Utilities::getCachedPixmap(QLatin1Literal(":/images/sort_ascending.png"));
            break;
        }
        case SORT_DEFAULT:
        {
            icon = QIcon();
            break;
        }
    }
    header->setIcon(icon);
}
