#pragma once
#include <QObject>

class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void headReadsHeadersWithoutWaitingForBody();
    void initTestCase();
    void variablesRespectDisabledEntriesAndReportMissing();
    void postJsonQueryHeadersAndBearerReachTheServer();
    void basicAuthAndFormEncodingReachTheServer();
    void httpErrorKeepsStatusBodyAndHeaders();
    void timeoutFinishesAndAllowsAnotherRequest();
    void cancelFinishesOnceAndReleasesBusyState();
    void invalidSchemeDoesNotStartNetworkOperation();
    void missingVariablesRejectTheRequestButDisabledFieldsDoNot();
    void redirectsFollowOnlyWithinTheSameOrigin();
    void manualRedirectReturnsTheOriginalResponse();
    void oversizedResponseIsExplicitlyTruncated();
    void workspacePersistsRequestHistoryAndEnvironment();
    void corruptWorkspaceIsReportedAndNotOverwritten();
    void nestedPostmanCollectionImportsAndRoundTrips();
};
