/*
 * Copyright (C) 2012 Jolla Ltd. <chris.adams@jollamobile.com>
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * "Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Nemo Mobile nor the names of its contributors
 *     may be used to endorse or promote products derived from this
 *     software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
 */

#include "identityinterface.h"
#include "identityinterface_p.h"

#include "sessiondatainterface.h"

#include <QtDebug>

//libsignon-qt
#include <SignOn/Identity>
#include <SignOn/identityinfo.h>

IdentityInterfacePrivate::IdentityInterfacePrivate(SignOn::Identity *ident, IdentityInterface *parent)
    : QObject(parent)
    , q(parent)
    , identity(ident)
    , session(0)
    , componentComplete(false)
    , initializationComplete(false)
    , pendingSync(false)
    , userNamePendingInit(false)
    , secretPendingInit(false)
    , captionPendingInit(false)
    , realmsPendingInit(false)
    , ownerPendingInit(false)
    , accessControlListPendingInit(false)
    , methodMechanismsPendingInit(false)
    , identifier(0)
    , identifierPending(false)
    , status(IdentityInterface::Initializing)
    , error(IdentityInterface::NoError)
{
    if (ident)
        setIdentity(ident);
}

IdentityInterfacePrivate::~IdentityInterfacePrivate()
{
}

void IdentityInterfacePrivate::setIdentity(SignOn::Identity *ident)
{
    if (!ident) {
        qWarning() << "IdentityInterfacePrivate::setIdentity() called with null identity! Aborting operation.";
        return;
    }

    identity = ident;
    identifier = identity->id();
    if (status != IdentityInterface::Initializing) {
        status = IdentityInterface::Initializing;
        bool needsEmit = false;
        if (!statusMessage.isEmpty()) {
            statusMessage = QString();
            needsEmit = true;
        }
        emit q->statusChanged();
        if (needsEmit) {
            emit q->statusMessageChanged();
        }
    }

    connect(identity, SIGNAL(info(SignOn::IdentityInfo)), this, SLOT(handleInfo(SignOn::IdentityInfo)));
    connect(identity, SIGNAL(removed()), this, SLOT(handleRemoved()));
    connect(identity, SIGNAL(error(SignOn::Error)), this, SLOT(handleError(SignOn::Error)));
    connect(identity, SIGNAL(credentialsStored(quint32)), this, SLOT(handleCredentialsStored(quint32)));

    // first time read from db.  we should be in Initializing state to begin with.
    // QueuedConnection to ensure that clients have a chance to connect to state changed signals.
    QMetaObject::invokeMethod(this, "asyncQueryInfo", Qt::QueuedConnection);
}

void IdentityInterfacePrivate::asyncQueryInfo()
{
    if (!identity) {
        qWarning() << "IdentityInterface: no identity set!  Maybe you forgot to call componentComplete()?";
        setStatus(IdentityInterface::Invalid);
        return;
    }

    identity->queryInfo();
}

void IdentityInterfacePrivate::handleInfo(const SignOn::IdentityInfo &retInfo)
{
    if (status == IdentityInterface::Invalid)
        return;

    bool pendingInitModifications = false;

    // refresh completed.
    // Overwrite all existing info property values, but only
    // emit change signals where required.
    info = retInfo;

    int currentIdentifier = identity->id();
    if (identifier != currentIdentifier) {
        identifier = currentIdentifier;
        emit q->identifierChanged();
    }

    if (userNamePendingInit) {
        info.setUserName(userName);
        pendingInitModifications = true;
        userNamePendingInit = false;
    } else if (userName != info.userName()) {
        userName = info.userName();
        emit q->userNameChanged();
    }

    if (secretPendingInit) {
        info.setSecret(secret);
        pendingInitModifications = true;
        secretPendingInit = false;
    } else {
        // Cannot read the secret.
        // they either supply it (and have write privs)
        // or need to ask the user.
    }

    if (captionPendingInit) {
        info.setCaption(caption);
        pendingInitModifications = true;
        captionPendingInit = false;
    } else if (caption != info.caption()) {
        caption = info.caption();
        emit q->captionChanged();
    }

    if (realmsPendingInit) {
        info.setRealms(realms);
        pendingInitModifications = true;
        realmsPendingInit = false;
    } else if (realms != info.realms()) {
        realms = info.realms();
        emit q->realmsChanged();
    }

    if (ownerPendingInit) {
        info.setOwner(owner);
        pendingInitModifications = true;
        ownerPendingInit = false;
    } else if (owner != info.owner()) {
        owner = owner;
        emit q->ownerChanged();
    }

    if (accessControlListPendingInit) {
        info.setAccessControlList(accessControlList);
        pendingInitModifications = true;
        accessControlListPendingInit = false;
    } else if (accessControlList != info.accessControlList()) {
        accessControlList = info.accessControlList();
        emit q->accessControlListChanged();
    }

    if (methodMechanismsPendingInit) {
        QStringList existingMethods = info.methods();
        foreach (const QString &meth, existingMethods)
            info.removeMethod(meth);
        QStringList newMethods = methodMechanisms.keys();
        foreach (const QString &method, newMethods)
            info.setMethod(method, methodMechanisms.value(method));
        pendingInitModifications = true;
        captionPendingInit = false;
    } else {
        QMap<QString, QStringList> infoMethodMechs;
        QStringList infoMeths = info.methods();
        foreach (const QString &method, infoMeths)
            infoMethodMechs.insert(method, info.mechanisms(method));
        if (methodMechanisms != infoMethodMechs) {
            methodMechanisms = infoMethodMechs;
            emit q->methodsChanged();
        }
    }

    if (status == IdentityInterface::Invalid || status == IdentityInterface::Error) {
        // error occurred during initialization/refresh, or was removed.
        // do nothing - the client will have already been notified.
    } else {
        // initialization/refresh completed successfully.
        if (!initializationComplete) {
            initializationComplete = true; // first time update complete.
            setStatus(IdentityInterface::Initialized);
            if (pendingInitModifications)
                setStatus(IdentityInterface::Modified); // modifications occurred prior to initialization completion.
            if (pendingSync) {
                pendingSync = false;
                q->sync(); // the user requested sync() while we were initializing.
            }
        } else {
            setStatus(IdentityInterface::Synced); // we're up to date now.
        }
    }
}

void IdentityInterfacePrivate::handleCredentialsStored(quint32 newId)
{
    if (status == IdentityInterface::Invalid)
        return;

    int currentIdentifier = newId;
    if (identifier != currentIdentifier) {
        identifier = currentIdentifier;
        emit q->identifierChanged();
    }

    setStatus(IdentityInterface::Synced);
}

void IdentityInterfacePrivate::handleRemoved()
{
    setStatus(IdentityInterface::Invalid);
}

void IdentityInterfacePrivate::handleError(SignOn::Error err)
{
    error = static_cast<IdentityInterface::ErrorType>(err.type());
    errorMessage = err.message();
    setStatus(IdentityInterface::Error);
    emit q->errorChanged();
    emit q->errorMessageChanged();
}

void IdentityInterfacePrivate::setStatus(IdentityInterface::Status newStatus, const QString &message)
{
    if (status == IdentityInterface::Invalid)
        return; // never update state from invalid to anything else.

    bool shouldEmit = false;
    if (statusMessage != message) {
        shouldEmit = true;
        statusMessage = message;
    }

    if (status != newStatus) {
        status = newStatus;
        emit q->statusChanged();
    }

    if (shouldEmit)
        emit q->statusMessageChanged(); // emit after statusChanged signal.
}

// AuthSession response
void IdentityInterfacePrivate::handleResponse(const SignOn::SessionData &sd)
{
    QVariantMap dataMap;
    QStringList keys = sd.propertyNames();
    foreach (const QString &key, keys)
        dataMap.insert(key, sd.getProperty(key));
    emit q->responseReceived(dataMap);
}

// AuthSession status
void IdentityInterfacePrivate::handleStateChanged(SignOn::AuthSession::AuthSessionState newState, const QString &message)
{
    setStatus(static_cast<IdentityInterface::Status>(newState), message);
}

// AuthSession signals
void IdentityInterfacePrivate::setUpSessionSignals()
{
    connect(session, SIGNAL(error(SignOn::Error)), this, SLOT(handleError(SignOn::Error)));
    connect(session, SIGNAL(response(SignOn::SessionData)), this, SLOT(handleResponse(SignOn::SessionData)));
    connect(session, SIGNAL(stateChanged(AuthSession::AuthSessionState, QString)),
            this, SLOT(handleStateChanged(AuthSession::AuthSessionState, QString)));
}

//---------------------------

/*!
    \qmltype Identity
    \instantiates IdentityInterface
    \inqmlmodule org.nemomobile.signon 1
    \brief An Identity which contains credentials used for service signon

    This type is intended for use by privileged applications
    and account plugin providers.  It allows service signon
    credentials to be specified.

    If the \c identifier property is specified in the object declaration,
    an existing identity with the specified identifier will be loaded from
    the database.  If the \c identifier property is not specified in the
    object declaration, a new identity will be created.

    Once an identity has been created, it is usual to associate
    an account with the identity, in order to allow client
    applications to use that account to sign on to the services
    for which the credentials in the identiy are valid.

    Example of usage to create a new account associated with a new identity:

    \qml
    import QtQuick 1.1
    import org.nemomobile.accounts 1.0
    import org.nemomobile.signon 1.0

    Item {
        id: root

        // the following 3 things would be populated from the UI.
        property string userName: "username"
        property string secret: "password"
        property string caption: "caption"

        // this would be triggered by user clicking "save" button
        function createIdentityAndAccount() {
            identity.setAndSync()
        }

        Identity {
            id: identity

            userName: root.userName
            secret: root.secret
            caption: root.caption

            function setAndSync() {
                setMethodMechanisms("password", ["ClientLogin"])
                sync() // store to db
            }

            onStatusChanged: {
                if (status == Identity.Synced) {
                    // successfully created identity.  now create the account.
                    account.setAndSync()
                }
            }
        }

        Account {
            id: account

            providerName: "google"
            displayName: "example account"

            function setAndSync() {
                enableAccountWithService("google-talk")
                setIdentityIdentifier(identity.identifier, "google-talk")
                sync() // store to db
            }

            onStatusChanged: {
                if (status == Identity.Synced) {
                    // successfully created account.  It will use the credentials
                    // from the identity to sign on to the "google-talk" service.
                }
            }
        }
    }
    \endqml

    Example of usage to edit an existing account and its associated identity:

    \qml
    import QtQuick 1.1
    import org.nemomobile.accounts 1.0
    import org.nemomobile.signon 1.0

    Item {
        id: root

        // the following account id would have been supplied when
        // the user selected which account they wished to edit
        property int accountId

        // the following 3 things would be used to populate the UI
        property string userName: identity.userName
        property string secret: identity.secret
        property string caption: identity.caption

        // this would be triggered by user clicking "save" button
        function createIdentityAndAccount() {
            identity.setAndSync()
        }

        Identity {
            id: identity
            identifierPending: true

            function setAndSync() {
                // update with modified data from UI
                userName = root.userName
                secret = root.secret
                caption = root.caption
                sync() // store to db
            }

            onStatusChanged: {
                if (status == Identity.Synced) {
                    // successfully updated the credentials!
                }
            }
        }

        Account {
            id: account
            identifier: accountId

            onStatusChanged: {
                if (status == Identity.Initialized) {
                    // retrieve the credentials and set the Identity's id
                    identity.identifier = account.identityIdentifier("google-talk")
                }
            }
        }
    }
    \endqml
*/

IdentityInterface::IdentityInterface(QObject *parent)
    : QObject(parent), d(new IdentityInterfacePrivate(0, this))
{
}

IdentityInterface::IdentityInterface(SignOn::Identity *ident, QObject *parent)
    : QObject(parent), d(new IdentityInterfacePrivate(ident, this))
{
}

IdentityInterface::~IdentityInterface()
{
    signOut(); // sign out of the currently active authsession
}

// QDeclarativeParserStatus
void IdentityInterface::classBegin() { }
void IdentityInterface::componentComplete()
{
    d->componentComplete = true;
    if (!d->identifierPending) {
        if (!d->identity) {
            SignOn::Identity *newIdent = 0;
            if (d->identifier == 0) // creating a new identity
                newIdent = SignOn::Identity::newIdentity(SignOn::IdentityInfo(), this); // owned by this adapter.
            else                    // loading an existing identity
                newIdent = SignOn::Identity::existingIdentity(d->identifier, this); // owned by this adapter.
            d->setIdentity(newIdent);
        } else {
            // identity was provided by IdentityManager.
            // do nothing.
        }
    }
}

/*!
    \qmlproperty int Identity::identifier
    Contains the identifier of the identity.

    If the \c identifierPending property is set to true, the identity
    will be initialized when the \c identifier is set.

    If the \c identifier is not set during initialization, or it is
    set to zero after component completion with \c identifierPending
    true, a new identity will be created.

    If the \c identifier is set, the identity in the database
    with the specified identifier will be loaded.
*/

int IdentityInterface::identifier() const
{
    return d->identifier;
}

void IdentityInterface::setIdentifier(int id)
{
    if (d->status == IdentityInterface::Initializing) {
        bool needsEmit = (d->identifier != id);
        d->identifier = id;
        if (d->componentComplete && d->identifierPending) {
            d->identifierPending = false;
            emit identifierPendingChanged();
            if (needsEmit)
                emit identifierChanged();
            componentComplete(); // now initialize.
        }
    } else if (id != 0 && id != d->identifier) {
        // changing identifier after initialisation.
        if (d->identity && d->identity->parent() == this) {
            disconnect(d->identity);
            d->identity->deleteLater();
        }

        d->identity = 0;
        d->identifier = id;
        componentComplete();
    }
}

/*!
    \qmlproperty int Identity::identifierPending

    This property should be set to true if the Identity is an existing
    identity, but the identifier is not available at initialization time.
    When this property is set, the Identity will remain in the Initializing
    state until the identifier is set.

    If this property is not set, and no identifier is set for the Identity,
    a new signon identity will be instantiated for the Identity.
*/
bool IdentityInterface::identifierPending() const
{
    return d->identifierPending;
}

void IdentityInterface::setIdentifierPending(bool p)
{
    if (!d->componentComplete && d->status == IdentityInterface::Initializing && d->identifierPending != p) {
        d->identifierPending = p;
        emit identifierPendingChanged();
    }
}

/*!
    \qmlproperty IdentityInterface::Status Identity::status
    Contains the status of the identity
*/

IdentityInterface::Status IdentityInterface::status() const
{
    return d->status;
}

/*!
    \qmlproperty string Identity::statusMessage
    Contains the message associated with the most recent
    status changed which occurred during sign-in.  Note
    that status changes unrelated to sign-in will not
    have any message associated.
*/

QString IdentityInterface::statusMessage() const
{
    return d->statusMessage;
}

/*!
    \qmlproperty IdentityInterface::Error Identity::error
    Contains the most recent error which occurred during
    creation or synchronisation of the identity.  Note that
    this property will NOT be updated to NoError if subsequent
    synchronisation operations succeed.
*/
    

IdentityInterface::ErrorType IdentityInterface::error() const
{
    return d->error;
}

/*!
    \qmlproperty string Identity::errorMessage
    Contains the message associated with the most recent
    error which occurred during creation or synchronisation
    of the identity.  Note that this property will NOT
    be updated to NoError if subsequent synchronisation
    operations succeed.
*/

QString IdentityInterface::errorMessage() const
{
    return d->errorMessage;
}

/*!
    \qmlproperty string Identity::userName
    Contains the user name part of the credentials which may be
    used to sign in to services associated with this identity.
*/

QString IdentityInterface::userName() const
{
    return d->userName;
}

void IdentityInterface::setUserName(const QString &uname)
{
    if (d->status != IdentityInterface::Invalid
            && d->status != IdentityInterface::SyncInProgress
            && d->status != IdentityInterface::RefreshInProgress
            && d->userName != uname) {
        d->userName = uname;
        if (d->initializationComplete)
            d->setStatus(IdentityInterface::Modified);
        else
            d->userNamePendingInit = true;
        emit userNameChanged();
    }
}

/*!
    \qmlproperty string Identity::secret
    Contains the secret part of the credentials which may be
    used to sign in to services associated with this Identity.
    Note that the secret cannot be read from the database, and
    thus must either be provided when creating the identity,
    or will be empty.

    When calling sync() to update the database, you must explicitly
    define the mode with which the synchronisation will occur.
    One of the modes causes the value of the secret associated with
    this identity in the database to be overwritten, whilst the
    other discards the Identity's secret when synchronising.
*/

QString IdentityInterface::secret() const
{
    return d->secret;
}

void IdentityInterface::setSecret(const QString &sec)
{
    if (d->status != IdentityInterface::Invalid
            && d->status != IdentityInterface::SyncInProgress
            && d->status != IdentityInterface::RefreshInProgress
            && d->secret != sec) {
        d->secret = sec;
        if (d->initializationComplete)
            d->setStatus(IdentityInterface::Modified);
        else
            d->secretPendingInit = true;
        emit secretChanged();
    }
}

/*!
    \qmlproperty string Identity::caption
    Contains the caption part of the credentials which may be
    used to sign in to services associated with this Identity.
    The caption may be required to determine whether the user
    is likely to be a human or not.
*/

QString IdentityInterface::caption() const
{
    return d->caption;
}

void IdentityInterface::setCaption(const QString &cap)
{
    if (d->status != IdentityInterface::Invalid
            && d->status != IdentityInterface::SyncInProgress
            && d->status != IdentityInterface::RefreshInProgress
            && d->caption != cap) {
        d->caption = cap;
        if (d->initializationComplete)
            d->setStatus(IdentityInterface::Modified);
        else
            d->captionPendingInit = true;
        emit captionChanged();
    }
}

/*!
    \qmlproperty QStringList Identity::realms
    Contains the realms (or domains) for which the credentials
    encapsulated by this Identity are valid.  For example,
    several different online services provided by the same provider
    may all be accessible using the same credentials.
*/

QStringList IdentityInterface::realms() const
{
    return d->realms;
}

void IdentityInterface::setRealms(const QStringList &rlms)
{
    if (d->status != IdentityInterface::Invalid
            && d->status != IdentityInterface::SyncInProgress
            && d->status != IdentityInterface::RefreshInProgress
            && d->realms != rlms) {
        d->realms = rlms;
        if (d->initializationComplete)
            d->setStatus(IdentityInterface::Modified);
        else
            d->realmsPendingInit = true;
        emit realmsChanged();
    }
}

/*!
    \qmlproperty string Identity::owner
    Contains the owner of the Identity
*/

QString IdentityInterface::owner() const
{
    return d->owner;
}

void IdentityInterface::setOwner(const QString &own)
{
    if (d->status != IdentityInterface::Invalid
            && d->status != IdentityInterface::SyncInProgress
            && d->status != IdentityInterface::RefreshInProgress
            && d->owner != own) {
        d->owner = own;
        if (d->initializationComplete)
            d->setStatus(IdentityInterface::Modified);
        else
            d->ownerPendingInit = true;
        emit ownerChanged();
    }
}

/*!
    \qmlproperty QStringList Identity::accessControlList
    Contains the access control list associated with the credentials
    encapsulated by this Identity.
*/

QStringList IdentityInterface::accessControlList() const
{
    return d->accessControlList;
}

void IdentityInterface::setAccessControlList(const QStringList& acl)
{
    if (d->status != IdentityInterface::Invalid
            && d->status != IdentityInterface::SyncInProgress
            && d->status != IdentityInterface::RefreshInProgress
            && d->accessControlList != acl) {
        d->accessControlList = acl;
        if (d->initializationComplete)
            d->setStatus(IdentityInterface::Modified);
        else
            d->accessControlListPendingInit = true;
        emit accessControlListChanged();
    }
}

/*!
    \qmlproperty QStringList Identity::methods
    Contains the names of methods which are supported as
    authentication methods by the credentials encapsulated
    by this Identity.  Different services may require different
    methods to be used during authentication.
*/

QStringList IdentityInterface::methods() const
{
    return d->methodMechanisms.keys();
}


/*!
    \qmlmethod void Identity::setMethodMechanisms(const QString &methodName, const QStringList &mechanisms)
    Sets the mechanisms for the method identified by the given \a methodName to
    \a mechanisms.  Different services may require different mechanisms to be
    used during authentication, even if they use the same method.
*/
void IdentityInterface::setMethodMechanisms(const QString &methodName, const QStringList &mechanisms)
{
    if (d->status != IdentityInterface::Invalid
            && d->status != IdentityInterface::SyncInProgress
            && d->status != IdentityInterface::RefreshInProgress) {
        if (!d->methodMechanisms.contains(methodName)
                || d->methodMechanisms.value(methodName) != mechanisms) {
            d->methodMechanisms.insert(methodName, mechanisms);
            if (d->initializationComplete)
                d->setStatus(IdentityInterface::Modified);
            else
                d->methodMechanismsPendingInit = true;
            emit methodsChanged();
        }
    }
}

/*!
    \qmlmethod QStringList Identity::methodMechanisms(const QString &methodName)
    Returns the mechanisms associated with the method identified by
    the given \a methodName
*/
QStringList IdentityInterface::methodMechanisms(const QString &methodName)
{
    return d->methodMechanisms.value(methodName);
}

/*!
    \qmlmethod void Identity::removeMethod(const QString &methodName)
    Removes the method (and any associated mechanisms) from the list of
    methods supported by the credentials encapsulated by this Identity.
*/
void IdentityInterface::removeMethod(const QString &methodName)
{
    if (d->status != IdentityInterface::Invalid
            && d->status != IdentityInterface::SyncInProgress
            && d->status != IdentityInterface::RefreshInProgress) {
        d->methodMechanisms.remove(methodName);
        if (d->initializationComplete)
            d->setStatus(IdentityInterface::Modified);
        else
            d->methodMechanismsPendingInit = true;
        emit methodsChanged();
    }
}

/*!
    \qmlmethod void Identity::sync(Identity::SyncMode mode)

    May only be called if the current status is Modified.
    Calling sync() if the status has any other value, will have no effect.
    After calling sync(), the status will change to SyncInProgress,
    and a (possibly, but not certainly, asynchronous) database write will occur.
    Once the data has been written to the database (or an error has occurred),
    the status will change to Synced (or Error).

    You can not make any changes to properties whilst in the SyncInProgress
    state.

    If the \a mode parameter is Identity::OverwriteSecret, the secret of the underlying
    identity will be overwitten by the sync() process, otherwise if the mode is
    Identity::NoOverwriteSecret the database  version of the secret will be left
    intact.
*/
void IdentityInterface::sync(IdentityInterface::SyncMode mode)
{
    if (d->status == IdentityInterface::Initializing)
        d->pendingSync = true;

    if (d->status != IdentityInterface::Modified)
        return;

    if (mode == IdentityInterface::OverwriteSecret) {
        d->info.setSecret(d->secret);
        d->info.setStoreSecret(true);
    } else if (mode == IdentityInterface::NoOverwriteSecret) {
        d->info.setStoreSecret(false);
    }

    d->info.setUserName(d->userName);
    d->info.setCaption(d->caption);
    d->info.setRealms(d->realms);
    d->info.setOwner(d->owner);
    d->info.setAccessControlList(d->accessControlList);
    QStringList existingMethods = d->info.methods();
    foreach (const QString &meth, existingMethods)
        d->info.removeMethod(meth);
    QStringList newMethods = d->methodMechanisms.keys();
    foreach (const QString &method, newMethods)
        d->info.setMethod(method, d->methodMechanisms.value(method));

    d->setStatus(IdentityInterface::SyncInProgress);
    d->identity->storeCredentials(d->info); // the docs are a bit unclear.. I assume this does what I think it does.
}

/*!
    \qmlmethod void Identity::refresh()

    Triggers refresh of identity data by reading from the database.

    May only be called if the current status is Initialized, Synced, Modified or Error.
    Calling refresh() if status has any other value, will have no effect.
    After calling refresh(), the status will change to RefreshInProgress,
    and a (possibly, but not certainly, asynchronous) database read will occur.
    Once the data has been retrieved from the database (or an error has occurred),
    the status will change to Synced and the property values will be updated to the
    newly retrieved database state (or the status will change to Error).

    Note that if an error occurred during creation of the identity, the first
    time refresh() succeeds, the state will transition to Initialized rather
    than Synced.

    Note that the secret cannot be refreshed; if you have access to write it,
    you may attempt to do so, but it cannot be read back.

    Any changes you have made to any properties will be overwritten when the
    refresh completes.
*/
void IdentityInterface::refresh()
{
    if (d->status != IdentityInterface::Synced
            && d->status != IdentityInterface::Initialized
            && d->status != IdentityInterface::Modified
            && d->status != IdentityInterface::Error)
        return;

    if (d->pendingSync
            && (d->status == IdentityInterface::Initialized
            ||  d->status == IdentityInterface::Modified))
        return; // we're actually just about to trigger a sync() in d->handleInfo().

    d->setStatus(IdentityInterface::RefreshInProgress);
    d->identity->queryInfo();
}

/*!
    \qmlmethod void Identity::remove()

    Triggers removal of the identity from the database.
    When the identity is removed, it will become Invalid.
*/
void IdentityInterface::remove()
{
    if (!d->identity)
        return;

    // since we are removing the SignOn::Identity itself, we not only sign
    // out of the active auth session via IdentityInterface::signOut() but
    // we actually sign out of all active auth sessions via
    // SignOn::Identity::signOut().
    d->identity->signOut();

    // remove the identity
    d->setStatus(IdentityInterface::SyncInProgress);
    d->identity->remove();
    d->identity = 0; // seems like libsignon-qt doesn't propagate the signal correctly.  assume it worked...
    d->setStatus(IdentityInterface::Invalid);
}

// ------------------------------------ sign-on related functions

/*!
    \qmlmethod void Identity::verifySecret(const QString &secret)

    Triggers verification of the given \a secret.
    The verification may occur synchronously or asynchronously.
    When verification completes, the secretVerified() signal
    will be emitted.
*/
void IdentityInterface::verifySecret(const QString &secret)
{
    if (d->status == IdentityInterface::Invalid)
        return;
    d->identity->verifySecret(secret);
}


/*!
    \qmlmethod void Identity::requestCredentialsUpdate(const QString &message)

    Triggers out-of-process dialogue creation which requests
    updated credentials from the user.

    The implementation of this function depends on the
    implementation of the underlying signon-ui service.
*/
void IdentityInterface::requestCredentialsUpdate(const QString &message)
{
    if (d->status == IdentityInterface::Invalid)
        return;
    d->identity->requestCredentialsUpdate(message);
}

/*!
    \qmlmethod void Identity::verifyUser(const QString &message)
    Triggers out-of-process dialogue creation which attempts
    to verify the user, displaying the given \a message.

    The implementation of this function depends on the
    implementation of the underlying signon-ui service.
*/
void IdentityInterface::verifyUser(const QString &message)
{
    if (d->status == IdentityInterface::Invalid)
        return;
    d->identity->verifyUser(message);
}

/*!
    \qmlmethod void Identity::verifyUser(const QVariantMap &params)

    Triggers out-of-process dialogue creation which attempts
    to verify the user, specifying the given \a params.

    The implementation of this function depends on the
    implementation of the underlying signon-ui service.
*/
void IdentityInterface::verifyUser(const QVariantMap &params)
{
    if (d->status == IdentityInterface::Invalid)
        return;
    d->identity->verifyUser(SessionDataInterface::sanitizeVariantMap(params));
}

/*!
    \qmlmethod bool Identity::signIn(const QString &method, const QString &mechanism, const QVariantMap &sessionData)

    Begins sign-in to the service via the specified \a method and
    \a mechanism, using the given \a sessionData parameters.
    Returns true if the session could be created.
    Once the sign-in request has been processed successfully, the
    responseReceived() signal will be emitted.  If sign-in occurs
    in multiple stages (for example, via token swapping), you must
    call process() to progress the sign-in operation.

    Only one sign-in session may exist for an Identity at any
    time.  To start a new sign-in session, you must signOut() of the
    current sign-in session, if one exists, prior to calling signIn().

    Usually only one single method and one single mechanism will be valid
    to sign in with, and these can be retrieved from the \c authData of the
    \c ServiceAccount for the service with which this \c Identity is associated.

    The \a sessionData should be retrieved from the \c authData of the
    \c ServiceAccount and augmented with any application-specific values
    which are required for sign on.  In particular, it is often useful or
    necessary to provide one or more of the following keys:

    \table
        \header
            \li Key
            \li Value
            \li When
        \row
            \li WindowId
            \li You may call the \c windowId() function of the \c WindowIdHelper
                type to retrieve the window id.  Alternatively, it may be
                accessed from the top-level QDeclarativeView or QWidget.
            \li All applications should provide a \c WindowId in order to
                ensure that any UI spawned by the sign-on UI daemon will
                belong to the correct window.
        \row
            \li Embedded
            \li Either \c true or \c false.  It is \c false by default.
            \li If you wish to embed the UI spawned by the sign-on UI daemon
                into a SignOnUiContainer, this value should be set to \c true.
        \row
            \li Title
            \li A translated string
            \li The title will be displayed in any UI element spawned by the
                sign-on UI daemon.
        \row
            \li ClientId
            \li OAuth2 application client id provided to you by the service
                (eg, Facebook, GMail, etc)
            \li Services which use the \c oauth2 method with the \c user_agent
                mechanism require this key to be provided
        \row
            \li ClientSecret
            \li OAuth2 application client secret provided to you by the service
                (eg, Facebook, GMail, etc)
            \li Services which use the \c oauth2 method with the \c user_agent
                mechanism require this key to be provided
        \row
            \li ConsumerKey
            \li OAuth1.0a application consumer key provided to you by the service
                (eg, Flickr, Twitter, etc)
            \li Services which use the \c oauth1.0a or \c oauth2 method with the \c HMAC-SHA1
                mechanism require this key to be provided
        \row
            \li ConsumerSecret
            \li OAuth1.0a application consumer secret provided to you by the service
                (eg, Flickr, Twitter, etc)
            \li Services which use the \c oauth1.0a or \c oauth2 method with the \c HMAC-SHA1
                mechanism require this key to be provided
    \endtable
*/
bool IdentityInterface::signIn(const QString &method, const QString &mechanism, const QVariantMap &sessionData)
{
    if (d->status == IdentityInterface::Invalid
            || d->status == IdentityInterface::Initializing
            || !d->initializationComplete) {
        return false;
    }

    if (d->session) {
        qWarning() << Q_FUNC_INFO << "Sign-in requested while previous sign-in session exists!";
        return false; // not a continuation.  they need to sign out first.
    }

    // beginning new sign-on
    d->session = d->identity->createSession(method);

    if (!d->session) {
        qWarning() << Q_FUNC_INFO << "Failed to create sign-in session.";
        return false;
    }

    d->currentMethod = method;
    d->currentMechanism = mechanism;
    d->setUpSessionSignals();
    d->session->process(SignOn::SessionData(SessionDataInterface::sanitizeVariantMap(sessionData)), mechanism);
    return true;
}

/*!
    \qmlmethod void Identity::process(const QVariantMap &sessionData)
    Processes the session request defined by the given \a sessionData in order
    to progress a multi-stage sign-in operation.
*/
void IdentityInterface::process(const QVariantMap &sessionData)
{
    if (d->status == IdentityInterface::Invalid)
        return;

    if (d->session)
        d->session->process(SignOn::SessionData(SessionDataInterface::sanitizeVariantMap(sessionData)), d->currentMechanism);
}

/*!
    \qmlmethod void Identity::signOut()
    Signs out of the service, if a sign-in process has been initiated.
    The status of the Identity will be set to \c NotStarted.
*/
void IdentityInterface::signOut()
{
    if (d->status == IdentityInterface::Invalid)
        return;

    if (d->session) {
        SignOn::AuthSession *temp = d->session;
        d->session = 0;
        d->currentMethod = QString();
        d->currentMechanism = QString();
        d->identity->destroySession(temp);
        d->setStatus(IdentityInterface::NotStarted);
    }
}

// ------------------------------------

// the following functions are helpers for the IdentityManagerInterface ONLY
void IdentityInterface::setIdentity(SignOn::Identity *ident)
{
    if (d->identity) {
        qWarning() << "IdentityInterface::setIdentity() Warning! Already have identity set! Aborting operation.";
        return;
    }

    d->setIdentity(ident);
}

SignOn::Identity *IdentityInterface::identity() const
{
    return d->identity;
}

