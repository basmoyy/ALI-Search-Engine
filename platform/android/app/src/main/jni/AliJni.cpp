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

} // extern "C"

JNIEXPORT jint JNICALL
Java_com_ali_search_AliEngine_nativeIndexBytes(JNIEnv* env, jclass,
                                                jlong handle, jlongArray out) {
    uint64_t bytes = 0;
    const jint rc = ali_stats_index_bytes(reinterpret_cast<AliEngine*>(handle), &bytes);
    const jlong val = static_cast<jlong>(bytes);
    env->SetLongArrayRegion(out, 0, 1, &val);
    return rc;
}

// ── Discovery stats ───────────────────────────────────────────────────────────

JNIEXPORT jint JNICALL
Java_com_ali_search_AliEngine_nativeSourceCount(JNIEnv* env, jclass,
                                                 jlong handle, jintArray out) {
    uint32_t count = 0;
    const jint rc = ali_stats_source_count(reinterpret_cast<AliEngine*>(handle), &count);
    env->SetIntArrayRegion(out, 0, 1, reinterpret_cast<const jint*>(&count));
    return rc;
}

JNIEXPORT jint JNICALL
Java_com_ali_search_AliEngine_nativeNavEdgeCount(JNIEnv* env, jclass,
                                                  jlong handle, jintArray out) {
    uint32_t count = 0;
    const jint rc = ali_stats_nav_edge_count(reinterpret_cast<AliEngine*>(handle), &count);
    env->SetIntArrayRegion(out, 0, 1, reinterpret_cast<const jint*>(&count));
    return rc;
}

JNIEXPORT jint JNICALL
Java_com_ali_search_AliEngine_nativeGrammarCount(JNIEnv* env, jclass,
                                                  jlong handle, jintArray out) {
    uint32_t count = 0;
    const jint rc = ali_stats_grammar_count(reinterpret_cast<AliEngine*>(handle), &count);
    env->SetIntArrayRegion(out, 0, 1, reinterpret_cast<const jint*>(&count));
    return rc;
}

// ── Grammar-guided search ─────────────────────────────────────────────────────
// Returns up to maxResults grammar-constructed URLs as tab-separated:
// "grammar_name\turl\tdomain\tconfidence"

JNIEXPORT jobjectArray JNICALL
Java_com_ali_search_AliEngine_nativeGrammarConstruct(JNIEnv* env, jclass,
                                                      jlong handle,
                                                      jstring query,
                                                      jint maxResults) {
    const char* q = env->GetStringUTFChars(query, nullptr);

    uint32_t count = 0;
    jclass strClass = env->FindClass("java/lang/String");
    jobjectArray arr = nullptr;

    // Use the grammar construct C API.
    AliGrammarResult* results = nullptr;
    ali_grammar_construct(reinterpret_cast<AliEngine*>(handle), q,
                          static_cast<uint32_t>(maxResults), &results, &count);
    env->ReleaseStringUTFChars(query, q);

    arr = env->NewObjectArray(static_cast<jsize>(count), strClass, nullptr);
    for (uint32_t i = 0; i < count; ++i) {
        const char* name    = nullptr;
        const char* url     = nullptr;
        const char* domain  = nullptr;
        float       conf    = 0.0f;
        ali_grammar_result_get(results, i, &name, &url, &domain, &conf);
        const std::string row =
            std::string(name   ? name   : "") + '\t' +
            std::string(url    ? url    : "") + '\t' +
            std::string(domain ? domain : "") + '\t' +
            std::to_string(conf);
        env->SetObjectArrayElement(arr, static_cast<jsize>(i),
                                   env->NewStringUTF(row.c_str()));
    }
    ali_grammar_results_free(results);
    return arr;
}

// ── Enqueue seed URLs (bulk) ──────────────────────────────────────────────────

JNIEXPORT jint JNICALL
Java_com_ali_search_AliEngine_nativeEnqueueSeeds(JNIEnv* env, jclass,
                                                  jlong handle,
                                                  jobjectArray urlArray) {
    AliEngine* engine = reinterpret_cast<AliEngine*>(handle);
    const jsize n = env->GetArrayLength(urlArray);
    int failures = 0;
    for (jsize i = 0; i < n; ++i) {
        auto jstr = reinterpret_cast<jstring>(env->GetObjectArrayElement(urlArray, i));
        if (!jstr) continue;
        const char* u = env->GetStringUTFChars(jstr, nullptr);
        if (ali_crawler_enqueue_url(engine, u) != 0) ++failures;
        env->ReleaseStringUTFChars(jstr, u);
    }
    return failures == 0 ? 0 : -1;
}

} // extern "C"
