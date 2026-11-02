// AliJni.cpp — JNI bridge between Kotlin and the ALI C API.
//
// Each method maps directly to an ali.h function.
// The engine handle is stored as a `long` in Kotlin and cast here.

#include "ali.h"

#include <jni.h>
#include <string>

extern "C" {

// ── Engine ────────────────────────────────────────────────────────────────────

JNIEXPORT jlong JNICALL
Java_com_ali_search_AliEngine_nativeOpen(JNIEnv* env, jclass,
                                          jstring indexRoot, jstring dbPath) {
    const char* root = env->GetStringUTFChars(indexRoot, nullptr);
    const char* db   = env->GetStringUTFChars(dbPath,    nullptr);

    AliEngine* engine = nullptr;
    ali_engine_open(root, db, &engine);

    env->ReleaseStringUTFChars(indexRoot, root);
    env->ReleaseStringUTFChars(dbPath,    db);
    return reinterpret_cast<jlong>(engine);
}

JNIEXPORT void JNICALL
Java_com_ali_search_AliEngine_nativeClose(JNIEnv*, jclass, jlong handle) {
    ali_engine_close(reinterpret_cast<AliEngine*>(handle));
}

// ── Search ────────────────────────────────────────────────────────────────────

JNIEXPORT jobjectArray JNICALL
Java_com_ali_search_AliEngine_nativeSearch(JNIEnv* env, jclass,
                                            jlong handle,
                                            jstring queryText,
                                            jint topK,
                                            jint timeoutMs) {
    AliEngine* engine = reinterpret_cast<AliEngine*>(handle);
    const char* q = env->GetStringUTFChars(queryText, nullptr);

    AliResults* results = nullptr;
    ali_search(engine, q, static_cast<uint32_t>(topK),
               static_cast<uint32_t>(timeoutMs), &results);
    env->ReleaseStringUTFChars(queryText, q);

    if (!results) return nullptr;

    const uint32_t count = ali_results_count(results);

    // Return a String[] where each result is "url\ttitle\texcerpt\tscore"
    jclass  strClass = env->FindClass("java/lang/String");
    jobjectArray arr = env->NewObjectArray(static_cast<jsize>(count), strClass, nullptr);

    for (uint32_t i = 0; i < count; ++i) {
        const char *url = nullptr, *title = nullptr, *excerpt = nullptr;
        float score = 0.0f;
        ali_results_get(results, i, &url, &title, &excerpt, &score);

        const std::string row = std::string(url     ? url     : "") + '\t'
                              + std::string(title   ? title   : "") + '\t'
                              + std::string(excerpt ? excerpt : "") + '\t'
                              + std::to_string(score);
        env->SetObjectArrayElement(arr, static_cast<jsize>(i),
                                   env->NewStringUTF(row.c_str()));
    }

    ali_results_free(results);
    return arr;
}

// ── Index ─────────────────────────────────────────────────────────────────────

JNIEXPORT jint JNICALL
Java_com_ali_search_AliEngine_nativeIndexDocument(JNIEnv* env, jclass,
                                                   jlong handle,
                                                   jstring url, jstring title,
                                                   jstring body) {
    AliEngine* engine = reinterpret_cast<AliEngine*>(handle);
    const char* u = env->GetStringUTFChars(url,   nullptr);
    const char* t = env->GetStringUTFChars(title, nullptr);
    const char* b = env->GetStringUTFChars(body,  nullptr);
    const jint rc = ali_index_document(engine, u, t, b);
    env->ReleaseStringUTFChars(url,   u);
    env->ReleaseStringUTFChars(title, t);
    env->ReleaseStringUTFChars(body,  b);
    return rc;
}

JNIEXPORT jint JNICALL
Java_com_ali_search_AliEngine_nativeFlush(JNIEnv*, jclass, jlong handle) {
    return ali_index_flush(reinterpret_cast<AliEngine*>(handle));
}

// ── Crawler ───────────────────────────────────────────────────────────────────

JNIEXPORT jint JNICALL
Java_com_ali_search_AliEngine_nativeCrawlerStart(JNIEnv*, jclass, jlong handle) {
    return ali_crawler_start(reinterpret_cast<AliEngine*>(handle));
}

JNIEXPORT jint JNICALL
Java_com_ali_search_AliEngine_nativeCrawlerPause(JNIEnv*, jclass, jlong handle) {
    return ali_crawler_pause(reinterpret_cast<AliEngine*>(handle));
}

JNIEXPORT jint JNICALL
Java_com_ali_search_AliEngine_nativeCrawlerStop(JNIEnv*, jclass, jlong handle) {
    return ali_crawler_stop(reinterpret_cast<AliEngine*>(handle));
}

JNIEXPORT jint JNICALL
Java_com_ali_search_AliEngine_nativeCrawlerEnqueue(JNIEnv* env, jclass,
                                                    jlong handle, jstring url) {
    const char* u = env->GetStringUTFChars(url, nullptr);
    const jint rc = ali_crawler_enqueue_url(reinterpret_cast<AliEngine*>(handle), u);
    env->ReleaseStringUTFChars(url, u);
    return rc;
}

// ── Statistics ────────────────────────────────────────────────────────────────

JNIEXPORT jint JNICALL
Java_com_ali_search_AliEngine_nativeUpdateDeviceConditions(
        JNIEnv*, jclass, jlong handle,
        jint network_available, jint network_metered, jint charging,
        jint battery_level_pct, jint thermal_throttling,
        jint memory_pressure, jlong free_storage_bytes) {
    return ali_update_device_conditions(
        reinterpret_cast<AliEngine*>(handle),
        network_available, network_metered, charging,
        battery_level_pct, thermal_throttling, memory_pressure,
        static_cast<uint64_t>(free_storage_bytes));
}

JNIEXPORT jint JNICALL
Java_com_ali_search_AliEngine_nativeDocCount(JNIEnv* env, jclass,
                                              jlong handle, jlongArray out) {
    uint64_t count = 0;
    const jint rc = ali_stats_doc_count(reinterpret_cast<AliEngine*>(handle), &count);
    const jlong val = static_cast<jlong>(count);
    env->SetLongArrayRegion(out, 0, 1, &val);
    return rc;
}

JNIEXPORT jint JNICALL
Java_com_ali_search_AliEngine_nativeSegmentCount(JNIEnv* env, jclass,
                                                  jlong handle, jintArray out) {
    uint32_t count = 0;
    const jint rc = ali_stats_segment_count(reinterpret_cast<AliEngine*>(handle), &count);
    const jint val = static_cast<jint>(count);
    env->SetIntArrayRegion(out, 0, 1, &val);
    return rc;
}

JNIEXPORT jint JNICALL
Java_com_ali_search_AliEngine_nativeIndexBytes(JNIEnv* env, jclass,
                                                jlong handle, jlongArray out) {
    uint64_t bytes = 0;
    const jint rc = ali_stats_index_bytes(reinterpret_cast<AliEngine*>(handle), &bytes);
    const jlong val = static_cast<jlong>(bytes);
    env->SetLongArrayRegion(out, 0, 1, &val);
    return rc;
}

} // extern "C"
