#include "fennara/app_paths.hpp"
#include "fennara/tools/get_class_info/docs.hpp"
#include "fennara/tools/get_class_info/docs_internal.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/http_client.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/tls_options.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <map>
#include <string>

namespace fennara::get_class_info {

namespace {

constexpr int kHttpPortHttps = 443;
constexpr int kHttpTimeoutMs = 10000;
constexpr int kPollSleepMs = 10;
constexpr int64_t kCacheTtlSeconds = 7LL * 24LL * 60LL * 60LL;
constexpr const char *kDocsHost = "raw.githubusercontent.com";
const char *kDocModules[] = {
    "noise", "csg", "gridmap", "gdscript", "gltf", "multiplayer",
    "navigation_2d", "navigation_3d", "websocket", "webrtc", "openxr",
    "interactive_music", "regex", "text_server_adv", "text_server_fb",
    "theora", "vorbis", "mono", "fbx", "svg", "webxr", "mobile_vr",
    "mp3", "upnp", "jsonrpc", "enet", "zip",
    "godot_physics_2d", "godot_physics_3d", "jolt_physics",
};
std::map<std::string, ClassDocumentation> &parsed_docs_cache() {
    static std::map<std::string, ClassDocumentation> cache;
    return cache;
}

std::string _cache_key(const godot::String &class_name,
                       const godot::String &branch) {
    return std::string(branch.utf8().get_data()) + "::" +
           std::string(class_name.utf8().get_data());
}

godot::String _sanitize_path_component(const godot::String &value) {
    godot::String sanitized = value.strip_edges();
    sanitized = sanitized.replace("\\", "_");
    sanitized = sanitized.replace("/", "_");
    sanitized = sanitized.replace(":", "_");
    sanitized = sanitized.replace("*", "_");
    sanitized = sanitized.replace("?", "_");
    sanitized = sanitized.replace("\"", "_");
    sanitized = sanitized.replace("<", "_");
    sanitized = sanitized.replace(">", "_");
    sanitized = sanitized.replace("|", "_");
    return sanitized;
}

godot::String _cache_file_path(const godot::String &class_name,
                               const godot::String &branch) {
    const godot::String cache_root = app_paths::docs_cache_dir();
    if (cache_root.is_empty()) {
        return "";
    }

    return cache_root.path_join(_sanitize_path_component(branch))
        .path_join(_sanitize_path_component(class_name) + ".xml");
}

bool _read_cached_xml(const godot::String &class_name,
                      const godot::String &branch,
                      CachedXmlLookup &lookup) {
    const godot::String cache_path = _cache_file_path(class_name, branch);
    if (cache_path.is_empty() || !godot::FileAccess::file_exists(cache_path)) {
        return false;
    }

    godot::Ref<godot::FileAccess> file =
        godot::FileAccess::open(cache_path, godot::FileAccess::READ);
    if (file.is_null()) {
        return false;
    }

    lookup.found = true;
    lookup.xml_text = file->get_as_text();

    const int64_t modified_at = godot::FileAccess::get_modified_time(cache_path);
    const int64_t now = static_cast<int64_t>(
        godot::Time::get_singleton()->get_unix_time_from_system());
    lookup.fresh = modified_at > 0 && now > 0 &&
                   (now - modified_at) <= kCacheTtlSeconds;
    return true;
}

void _write_cached_xml(const godot::String &class_name,
                       const godot::String &branch,
                       const godot::String &xml_text) {
    const godot::String cache_path = _cache_file_path(class_name, branch);
    if (cache_path.is_empty()) {
        return;
    }

    const int64_t slash = cache_path.rfind("/");
    if (slash == -1) {
        return;
    }

    const godot::String dir_path = cache_path.substr(0, slash);
    const godot::Error dir_err =
        godot::DirAccess::make_dir_recursive_absolute(dir_path);
    if (dir_err != godot::OK && dir_err != godot::ERR_ALREADY_EXISTS) {
        return;
    }

    godot::Ref<godot::FileAccess> file =
        godot::FileAccess::open(cache_path, godot::FileAccess::WRITE);
    if (file.is_null()) {
        return;
    }

    file->store_string(xml_text);
}
HttpFetchResult _http_get_text(const godot::String &path) {
    HttpFetchResult result;

    godot::Ref<godot::HTTPClient> http;
    http.instantiate();
    if (http.is_null()) {
        result.error = "Failed to create HTTPClient.";
        return result;
    }

    godot::Error err =
        http->connect_to_host(kDocsHost, kHttpPortHttps, godot::TLSOptions::client());
    if (err != godot::OK) {
        result.error = "Failed to initiate HTTPS connection.";
        return result;
    }

    const uint64_t deadline =
        godot::Time::get_singleton()->get_ticks_msec() + kHttpTimeoutMs;
    bool request_sent = false;
    godot::String response_body;

    while (godot::Time::get_singleton()->get_ticks_msec() < deadline) {
        http->poll();
        godot::HTTPClient::Status status = http->get_status();

        if (status == godot::HTTPClient::STATUS_CANT_RESOLVE ||
            status == godot::HTTPClient::STATUS_CANT_CONNECT ||
            status == godot::HTTPClient::STATUS_TLS_HANDSHAKE_ERROR ||
            status == godot::HTTPClient::STATUS_CONNECTION_ERROR) {
            result.error = "Failed to connect to raw.githubusercontent.com.";
            return result;
        }

        if ((status == godot::HTTPClient::STATUS_CONNECTED) && !request_sent) {
            godot::PackedStringArray headers;
            headers.append("Accept: application/xml,text/plain;q=0.9,*/*;q=0.8");
            headers.append("User-Agent: Fennara/1.0");
            err = http->request(godot::HTTPClient::METHOD_GET, path, headers);
            if (err != godot::OK) {
                result.error = "Failed to send documentation request.";
                return result;
            }
            request_sent = true;
        }

        if (status == godot::HTTPClient::STATUS_BODY) {
            godot::PackedByteArray chunk = http->read_response_body_chunk();
            if (!chunk.is_empty()) {
                response_body += chunk.get_string_from_utf8();
            }
            if (http->get_status() != godot::HTTPClient::STATUS_BODY &&
                http->has_response()) {
                break;
            }
        } else if (request_sent && status == godot::HTTPClient::STATUS_CONNECTED &&
                   http->has_response()) {
            break;
        }

        godot::OS::get_singleton()->delay_msec(kPollSleepMs);
    }

    if (!http->has_response()) {
        result.error = "Timed out fetching Godot XML docs.";
        return result;
    }

    result.response_code = http->get_response_code();
    result.body = response_body;
    result.ok = result.response_code == 200;
    result.not_found = result.response_code == 404;
    if (!result.ok && !result.not_found) {
        result.error = "Unexpected HTTP response " +
                       godot::String::num_int64(result.response_code) +
                       " while fetching Godot XML docs.";
    }
    return result;
}

HttpFetchResult _curl_get_text(const godot::String &path) {
    HttpFetchResult result;

    godot::Array output;
    godot::PackedStringArray args;
    const godot::String marker = "__FENNARA_STATUS__";
    args.append("-L");
    args.append("-sS");
    args.append("-o");
    args.append("-");
    args.append("-w");
    args.append(marker + godot::String("%{http_code}"));
    args.append(godot::String("https://") + kDocsHost + path);

    int exit_code =
        godot::OS::get_singleton()->execute("curl", args, output, true);
    if (exit_code != 0) {
        result.error = "curl failed while fetching Godot XML docs.";
        return result;
    }
    if (output.is_empty()) {
        result.error = "curl returned no output while fetching Godot XML docs.";
        return result;
    }

    godot::String stdout_text = output[0];
    int marker_pos = stdout_text.rfind(marker);
    if (marker_pos == -1) {
        result.error =
            "curl output did not include an HTTP status marker.";
        return result;
    }

    result.body = stdout_text.substr(0, marker_pos);
    godot::String status_text =
        stdout_text.substr(marker_pos + marker.length()).strip_edges();
    result.response_code = status_text.to_int();
    result.ok = result.response_code == 200;
    result.not_found = result.response_code == 404;
    if (!result.ok && !result.not_found) {
        result.error = "curl returned HTTP " +
                       godot::String::num_int64(result.response_code) +
                       " while fetching Godot XML docs.";
    }
    return result;
}

HttpFetchResult _get_text_with_fallback(const godot::String &path) {
    HttpFetchResult curl_result = _curl_get_text(path);
    if (curl_result.ok || curl_result.not_found) {
        return curl_result;
    }

    HttpFetchResult http_result = _http_get_text(path);
    if (http_result.ok || http_result.not_found) {
        return http_result;
    }

    if (!http_result.error.is_empty()) {
        return http_result;
    }
    return curl_result;
}

bool _fetch_class_xml(const godot::String &class_name,
                      const godot::String &branch,
                      godot::String &xml_text,
                      godot::String &module_notice,
                      godot::String &fetch_message) {
    const godot::String base =
        godot::String("/godotengine/godot/") + branch + godot::String("/");

    HttpFetchResult core_result =
        _get_text_with_fallback(base + godot::String("doc/classes/") + class_name +
                                godot::String(".xml"));
    if (core_result.ok) {
        xml_text = core_result.body;
        return true;
    }
    if (!core_result.not_found) {
        fetch_message = core_result.error;
        return false;
    }

    for (const char *module_name : kDocModules) {
        const godot::String module_path =
            "modules/" + godot::String(module_name) + "/doc_classes/";
        HttpFetchResult module_result =
            _get_text_with_fallback(base + module_path + class_name +
                                    godot::String(".xml"));
        if (module_result.ok) {
            xml_text = module_result.body;
            module_notice = "  (found in " + module_path + ")";
            return true;
        }
        if (!module_result.not_found) {
            fetch_message = module_result.error;
            return false;
        }
    }

    fetch_message = "Class '" + class_name + "' not found on branch '" + branch +
                    "' (checked doc/classes/ and " +
                    godot::String::num_int64(
                        static_cast<int64_t>(sizeof(kDocModules) / sizeof(kDocModules[0]))) +
                    " modules).";
    return false;
}

} // namespace
ClassDocumentation fetch_and_parse_class_documentation(
    const godot::String &class_name,
    const godot::String &branch) {
    const std::string key = _cache_key(class_name, branch);
    auto &cache = parsed_docs_cache();
    auto cached_it = cache.find(key);
    if (cached_it != cache.end()) {
        return cached_it->second;
    }

    ClassDocumentation docs;
    docs.class_name = class_name;
    docs.branch = branch;

    CachedXmlLookup cached_xml;
    const bool has_cached_xml = _read_cached_xml(class_name, branch, cached_xml);
    if (has_cached_xml && cached_xml.fresh) {
        docs = parse_class_documentation_xml(class_name, branch, cached_xml.xml_text);
        cache[key] = docs;
        return docs;
    }

    godot::String xml_text = has_cached_xml ? cached_xml.xml_text : godot::String();
    godot::String module_notice;
    godot::String fetch_message;
    if (!_fetch_class_xml(class_name, branch, xml_text, module_notice,
                          fetch_message)) {
        if (has_cached_xml && !cached_xml.xml_text.is_empty()) {
            docs = parse_class_documentation_xml(class_name, branch, cached_xml.xml_text);
            docs.fetch_message = "Using stale cached docs after refresh failed: " + fetch_message;
            docs.module_notice = module_notice;
            cache[key] = docs;
            return docs;
        }

        docs.fetch_message = fetch_message;
        docs.module_notice = module_notice;
        cache[key] = docs;
        return docs;
    }

    _write_cached_xml(class_name, branch, xml_text);
    docs = parse_class_documentation_xml(class_name, branch, xml_text);
    docs.module_notice = module_notice;
    cache[key] = docs;
    return docs;
}

void warm_class_documentation_cache(const godot::PackedStringArray &class_names,
                                    const godot::String &branch) {
    for (int i = 0; i < class_names.size(); i++) {
        const godot::String class_name = class_names[i].strip_edges();
        if (class_name.is_empty()) {
            continue;
        }
        fetch_and_parse_class_documentation(class_name, branch);
    }
}

} // namespace fennara::get_class_info
