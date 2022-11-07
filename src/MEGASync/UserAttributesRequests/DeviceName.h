#ifndef DEVICENAMEATTRIBUTESREQUESTS_H
#define DEVICENAMEATTRIBUTESREQUESTS_H

#include <control/UserAttributesManager.h>

namespace UserAttributes
{
class DeviceName : public AttributeRequest
{
    Q_OBJECT

public:
    DeviceName(const QString& userEmail);

    static std::shared_ptr<DeviceName> requestDeviceName();

    void onRequestFinish(mega::MegaApi *, mega::MegaRequest *incoming_request, mega::MegaError *e) override;
    void requestAttribute() override;
    RequestInfo fillRequestInfo() override;

    bool isAttributeReady() const override;

    QString getDeviceName() const;
    QString getDefaultDeviceName();

signals:
    void attributeReady(const QString&);

private:
    void processGetDeviceNameCallback(mega::MegaRequest *incoming_request, mega::MegaError *e);
    void processSetDeviceNameCallback(mega::MegaError *e);

    void setDeviceNameAttribute();

    QString mDeviceName;
    int  mNameSuffix;
};
}

#endif // DEVICENAMEATTRIBUTESREQUESTS_H
