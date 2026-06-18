#include "fennara/tools/get_class_info/internal.hpp"

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace fennara::get_class_info {

namespace {

godot::Dictionary &inherits_chain_cache() {
    static godot::Dictionary cache;
    return cache;
}

godot::Dictionary &inherited_by_cache() {
    static godot::Dictionary cache;
    return cache;
}

} // namespace
godot::PackedStringArray collect_inherits_chain(const godot::String &class_name) {
    godot::Dictionary &cache = inherits_chain_cache();
    if (cache.has(class_name)) {
        return cache[class_name];
    }

    godot::PackedStringArray chain;
    auto *cdb = godot::ClassDBSingleton::get_singleton();
    godot::Dictionary seen;

    godot::String current = class_name;
    while (!current.is_empty()) {
        if (seen.has(current)) {
            break;
        }
        seen[current] = true;
        chain.append(current);
        current = cdb->get_parent_class(current);
    }

    cache[class_name] = chain;
    return chain;
}

godot::PackedStringArray collect_inherited_by(const godot::String &class_name) {
    godot::Dictionary &cache = inherited_by_cache();
    if (cache.is_empty()) {
        auto *cdb = godot::ClassDBSingleton::get_singleton();
        godot::PackedStringArray all_classes = cdb->get_class_list();

        for (int i = 0; i < all_classes.size(); i++) {
            const godot::String candidate = all_classes[i];
            const godot::String parent = cdb->get_parent_class(candidate);
            if (parent.is_empty()) {
                continue;
            }

            godot::PackedStringArray children =
                cache.has(parent) ? godot::PackedStringArray(cache[parent])
                                  : godot::PackedStringArray();
            children.append(candidate);
            cache[parent] = children;
        }
    }

    godot::PackedStringArray children =
        cache.has(class_name) ? godot::PackedStringArray(cache[class_name])
                              : godot::PackedStringArray();
    children.sort();
    return children;
}

} // namespace fennara::get_class_info
