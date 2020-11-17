
#include "Model.h"
#include "platform/Platform.h"

#include <assert.h>

using namespace mega;

#ifdef WIN32
extern Q_CORE_EXPORT int qt_ntfs_permission_lookup;
#endif

std::unique_ptr<Model> Model::model;

Model *Model::instance()
{
    if (!model)
    {
        model.reset(new Model());
    }
    return Model::model.get();
}

Model::Model() : QObject(), syncMutex(QMutex::Recursive)
{
    preferences = Preferences::instance();
}

bool Model::hasUnattendedDisabledSyncs() const
{
    return unattendedDisabledSyncs.size();
}

void Model::removeSyncedFolder(int num)
{
    assert(num <= configuredSyncs.size() && configuredSyncsMap.contains(configuredSyncs.at(num)));
    QMutexLocker qm(&syncMutex);
    auto cs = configuredSyncsMap[configuredSyncs.at(num)];
    if (cs->isActive())
    {
        deactivateSync(cs);
    }

    auto tag = cs->tag();

    assert(preferences->logged());
    preferences->removeSyncSetting(cs);
    configuredSyncsMap.remove(configuredSyncs.at(num));
    configuredSyncs.removeAt(num);

    removeUnattendedDisabledSync(tag);


    emit syncRemoved(cs);
}

void Model::removeSyncedFolderByTag(int tag)
{
    QMutexLocker qm(&syncMutex);
    if (!configuredSyncsMap.contains(tag))
    {
        return;
    }

    auto cs = configuredSyncsMap[tag];

    if (cs->isActive())
    {
        deactivateSync(cs);
    }
    assert(preferences->logged());

    preferences->removeSyncSetting(cs);
    configuredSyncsMap.remove(tag);

    auto it = configuredSyncs.begin();
    while (it != configuredSyncs.end())
    {
        if ((*it) == tag)
        {
            it = configuredSyncs.erase(it);
        }
        else
        {
            ++it;
        }
    }

    removeUnattendedDisabledSync(tag);

    emit syncRemoved(cs);
}

void Model::removeAllFolders()
{
    QMutexLocker qm(&syncMutex);
    assert(preferences->logged());

    //remove all configured syncs
    preferences->removeAllFolders();

    for (auto it = configuredSyncsMap.begin(); it != configuredSyncsMap.end(); it++)
    {
        if (it.value()->isActive())
        {
            deactivateSync(it.value());
        }
    }
    configuredSyncs.clear();
    configuredSyncsMap.clear();
    unattendedDisabledSyncs.clear();
}

void Model::activateSync(std::shared_ptr<SyncSetting> syncSetting)
{
#ifndef NDEBUG
    {
        auto sl = syncSetting->getLocalFolder();
#ifdef _WIN32
        if (sl.startsWith(QString::fromAscii("\\\\?\\")))
        {
            sl = sl.mid(4);
        }
#endif
        auto slc = QDir::toNativeSeparators(QFileInfo(sl).canonicalFilePath());
        assert(sl == slc);
    }
#endif

    // set sync UID
    if (syncSetting->getSyncID().isEmpty())
    {
        syncSetting->setSyncID(QUuid::createUuid().toString().toUpper());
    }

    //send event for the first sync
    if (!isFirstSyncDone && !preferences->isFirstSyncDone())
    {
        MegaSyncApp->getMegaApi()->sendEvent(99501, "MEGAsync first sync");
    }
    isFirstSyncDone = true;

    if ( !preferences->isFatWarningShown() && syncSetting->getError() == MegaSync::Error::LOCAL_IS_FAT)
    {
        QMegaMessageBox::warning(nullptr, tr("MEGAsync"),
         tr("You are syncing a local folder formatted with a FAT filesystem. That filesystem has deficiencies managing big files and modification times that can cause synchronization problems (e.g. when daylight saving changes), so it's strongly recommended that you only sync folders formatted with more reliable filesystems like NTFS (more information [A]here[/A]).")
         .replace(QString::fromUtf8("[A]"), QString::fromUtf8("<a href=\"https://help.mega.nz/megasync/syncing.html#can-i-sync-fat-fat32-partitions-under-windows\">"))
         .replace(QString::fromUtf8("[/A]"), QString::fromUtf8("</a>")));
        preferences->setFatWarningShown();
    }
    else if (!preferences->isOneTimeActionDone(Preferences::ONE_TIME_ACTION_HGFS_WARNING) && syncSetting->getError() == MegaSync::Error::LOCAL_IS_HGFS)
    {
        QMegaMessageBox::warning(nullptr, tr("MEGAsync"),
            tr("You are syncing a local folder shared with VMWare. Those folders do not support filesystem notifications so MEGAsync will have to be continuously scanning to detect changes in your files and folders. Please use a different folder if possible to reduce the CPU usage."));
        preferences->setOneTimeActionDone(Preferences::ONE_TIME_ACTION_HGFS_WARNING, true);
    }

    Platform::syncFolderAdded(syncSetting->getLocalFolder(), syncSetting->name(), syncSetting->getSyncID());
}

void Model::deactivateSync(std::shared_ptr<SyncSetting> syncSetting)
{
    Platform::syncFolderRemoved(syncSetting->getLocalFolder(), syncSetting->name(), syncSetting->getSyncID());
    MegaSyncApp->notifyItemChange(syncSetting->getLocalFolder(), MegaApi::STATE_NONE);
}

std::shared_ptr<SyncSetting> Model::updateSyncSettings(MegaSync *sync, int addingState)
{
    if (!sync)
    {
        return nullptr;
    }

    QMutexLocker qm(&syncMutex);

    std::shared_ptr<SyncSetting> cs;
    bool wasEnabled = true;
    bool wasActive = false;
    bool wasInactive = false;

    if (configuredSyncsMap.contains(sync->getTag())) //existing configuration (an update or a resume after picked from old sync config)
    {
        cs = configuredSyncsMap[sync->getTag()];

        wasEnabled = cs->isEnabled();
        wasActive = cs->isActive();
        wasInactive = !cs->isActive();

        cs->setSync(sync);
    }
    else //new configuration (new or resumed)
    {
        assert(addingState && "!addingState and didn't find previously configured sync");

        auto loaded = preferences->getLoadedSyncsMap();
        if (loaded.contains(sync->getTag())) //existing configuration from previous executions (we get the data that the sdk might not be providing from our cache)
        {
            cs = configuredSyncsMap[sync->getTag()] = std::make_shared<SyncSetting>(*loaded[sync->getTag()].get());
            cs->setSync(sync);
        }
        else // new addition (no reference in the cache)
        {
            cs = configuredSyncsMap[sync->getTag()] = std::make_shared<SyncSetting>(sync);
        }

        configuredSyncs.append(sync->getTag());
    }


    if (addingState) //new or resumed
    {
        wasActive = (addingState == MegaSync::SyncAdded::FROM_CACHE && cs->isActive() )
                || addingState == MegaSync::SyncAdded::FROM_CACHE_FAILED_TO_RESUME;

        wasInactive =  (addingState == MegaSync::SyncAdded::FROM_CACHE && !cs->isActive() )
                || addingState == MegaSync::SyncAdded::NEW || addingState == MegaSync::SyncAdded::FROM_CACHE_REENABLED
                || addingState == MegaSync::SyncAdded::REENABLED_FAILED;
    }

    if (cs->isActive() && wasInactive)
    {
        activateSync(cs);
    }

    if (!cs->isActive() && wasActive )
    {
        deactivateSync(cs);
    }

    preferences->writeSyncSetting(cs); // we store MEGAsync specific fields into cache

#ifdef WIN32
    // handle transition from MEGAsync <= 3.0.1.
    // if resumed from cache and the previous version did not have left pane icons, add them
    if (MegaSyncApp->getPrevVersion() && MegaSyncApp->getPrevVersion() <= 3001
            && !preferences->leftPaneIconsDisabled()
            && addingState == MegaSync::SyncAdded::FROM_CACHE && cs->isActive())
    {
        Platform::addSyncToLeftPane(cs->getLocalFolder(), cs->name(), cs->getSyncID());
    }
#endif

    emit syncStateChanged(cs);
    return cs;
}

void Model::rewriteSyncSettings()
{
    preferences->removeAllSyncSettings();
    QMutexLocker qm(&syncMutex);
    for (auto &cs : configuredSyncs)
    {
        preferences->writeSyncSetting(configuredSyncsMap[cs]); // we store MEGAsync specific fields into cache
    }
}

void Model::pickInfoFromOldSync(const SyncData &osd, int tag, bool loadedFromPreviousSessions)
{
    QMutexLocker qm(&syncMutex);
    assert(preferences->logged() || loadedFromPreviousSessions);
    std::shared_ptr<SyncSetting> cs;

    assert (!configuredSyncsMap.contains(tag) && "picking already configured sync!"); //this should always be the case

    cs = configuredSyncsMap[tag] = std::make_shared<SyncSetting>(osd, loadedFromPreviousSessions);

    cs->setTag(tag); //assign the new tag given by the sdk

    assert(!configuredSyncs.contains(tag));
    configuredSyncs.append(tag);

    preferences->writeSyncSetting(cs);
}

void Model::reset()
{
    QMutexLocker qm(&syncMutex);
    configuredSyncs.clear();
    configuredSyncsMap.clear();
    unattendedDisabledSyncs.clear();
    isFirstSyncDone = false;
}

int Model::getNumSyncedFolders()
{
    QMutexLocker qm(&syncMutex);
    return  configuredSyncs.size();
}

QStringList Model::getSyncNames()
{
    QMutexLocker qm(&syncMutex);
    QStringList value;
    for (auto &cs : configuredSyncs)
    {
        value.append(configuredSyncsMap[cs]->name());
    }
    return value;
}

QStringList Model::getSyncIDs()
{
    QMutexLocker qm(&syncMutex);
    QStringList value;
    for (auto &cs : configuredSyncs)
    {
        value.append(QString::number(configuredSyncsMap[cs]->tag()));
    }
    return value;
}

QStringList Model::getMegaFolders()
{
    QMutexLocker qm(&syncMutex);
    QStringList value;
    for (auto &cs : configuredSyncs)
    {
        value.append(configuredSyncsMap[cs]->getMegaFolder());
    }
    return value;
}

QStringList Model::getLocalFolders()
{
    QMutexLocker qm(&syncMutex);
    QStringList value;
    for (auto &cs : configuredSyncs)
    {
        value.append(configuredSyncsMap[cs]->getLocalFolder());
    }
    return value;
}

QList<long long> Model::getMegaFolderHandles()
{
    QMutexLocker qm(&syncMutex);
    QList<long long> value;
    for (auto &cs : configuredSyncs)
    {
        value.append(configuredSyncsMap[cs]->getMegaHandle());
    }
    return value;
}

std::shared_ptr<SyncSetting> Model::getSyncSetting(int num)
{
    QMutexLocker qm(&syncMutex);
    return configuredSyncsMap[configuredSyncs.at(num)];
}


std::shared_ptr<SyncSetting> Model::getSyncSettingByTag(int tag)
{
    QMutexLocker qm(&syncMutex);
    if (configuredSyncsMap.contains(tag))
    {
        return configuredSyncsMap[tag];
    }
    return nullptr;
}

void Model::saveUnattendedDisabledSyncs()
{
    if (preferences->logged())
    {
        preferences->setDisabledSyncTags(unattendedDisabledSyncs);
    }
}

void Model::addUnattendedDisabledSync(int tag)
{
    unattendedDisabledSyncs.insert(tag);
    saveUnattendedDisabledSyncs();
    emit syncDisabledListUpdated();
}

void Model::removeUnattendedDisabledSync(int tag)
{
    unattendedDisabledSyncs.remove(tag);
    saveUnattendedDisabledSyncs();
    emit syncDisabledListUpdated();
}

void Model::setUnattendedDisabledSyncs(QSet<int> tags)
{
    //REVIEW: If possible to get enable/disable callbacks before loading from settings.Merge both lists of tags.
    unattendedDisabledSyncs = tags;
    saveUnattendedDisabledSyncs();
    emit syncDisabledListUpdated();
}

void Model::dismissUnattendedDisabledSyncs()
{
    unattendedDisabledSyncs.clear();
    saveUnattendedDisabledSyncs();
    emit syncDisabledListUpdated();
}