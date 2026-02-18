#pragma once

#include <QtCore/QString>
#include <QtCore/QLoggingCategory>

#include <jni.h>

Q_DECLARE_LOGGING_CATEGORY(AndroidInterfaceLog)

namespace AndroidInterface
{
    void setImmersiveSticky(bool enable);
    bool cleanJavaException();
    jclass getActivityClass();
    void setNativeMethods();
    void jniLogDebug(JNIEnv *envA, jobject thizA, jstring messageA);
    void jniLogWarning(JNIEnv *envA, jobject thizA, jstring messageA);
    bool checkStoragePermissions();
    QString getSDCardPath();
    void setKeepScreenOn(bool on);

    constexpr const char *kJniQGCActivityClassName = "org/mavlink/qgroundcontrol/QGCActivity";
};
