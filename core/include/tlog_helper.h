/**
 * @file tlog_helper.h
 * @brief C++ STL container logging helpers for TLog
 *
 * This header provides helper functions to enable direct logging
 * of STL containers like std::vector, std::map, std::list, etc.
 *
 * Supports nested containers (e.g. vector<vector<int>>) via recursion.
 *
 * Usage:
 *   #include "tlog.h"
 *   #include "tlog_helper.h"
 *
 *   std::vector<int> vec = {1, 2, 3, 4, 5};
 *   TLOG_INFOF("Vector: {}", tlog::format(vec));
 *
 *   std::map<std::string, int> map = {{"a", 1}, {"b", 2}};
 *   TLOG_INFOF("Map: {}", tlog::format(map));
 */

#ifndef TLOG_HELPER_HPP
#define TLOG_HELPER_HPP

#ifdef __cplusplus

  #include <array>
  #include <deque>
  #include <forward_list>
  #include <list>
  #include <map>
  #include <memory>
  #include <optional>
  #include <set>
  #include <sstream>
  #include <string>
  #include <string_view>
  #include <tuple>
  #include <type_traits>
  #include <unordered_map>
  #include <unordered_set>
  #include <utility>
  #include <vector>
  #include <filesystem>


namespace tlog {

  // Forward declare the main entry point
  template <typename T> std::string format(const T &value);

  namespace detail {

    // Type traits to detect container categories
    template <typename T> struct is_string_type : std::false_type {};
    template <> struct is_string_type<std::string> : std::true_type {};
    template <> struct is_string_type<std::string_view> : std::true_type {};
    template <> struct is_string_type<const char *> : std::true_type {};
    template <> struct is_string_type<char *> : std::true_type {};

    // Helper to detect if a type is one of our supported containers
    // We use partial specializations for exact matching of supported standard containers

    struct ContainerTag {};
    struct MapTag : ContainerTag {};
    struct SetTag : ContainerTag {};
    struct SequenceTag : ContainerTag {};
    struct PairTag {};
    struct TupleTag {};
    struct OptionalTag {};
    struct SmartPtrTag {};
    struct PathTag {};
    struct UnknownTag {};

    template <typename T> struct get_type_tag {
      using type = UnknownTag;
    };

    // Sequences
    template <typename T, typename A> struct get_type_tag<std::vector<T, A>> {
      using type = SequenceTag;
    };
    template <typename T, typename A> struct get_type_tag<std::list<T, A>> {
      using type = SequenceTag;
    };
    template <typename T, typename A> struct get_type_tag<std::deque<T, A>> {
      using type = SequenceTag;
    };
    template <typename T, size_t N> struct get_type_tag<std::array<T, N>> {
      using type = SequenceTag;
    };
    template <typename T, typename A> struct get_type_tag<std::forward_list<T, A>> {
      using type = SequenceTag;
    };

    // Sets
    template <typename T, typename C, typename A> struct get_type_tag<std::set<T, C, A>> {
      using type = SetTag;
    };
    template <typename T, typename H, typename P, typename A>
    struct get_type_tag<std::unordered_set<T, H, P, A>> {
      using type = SetTag;
    };

    // Maps
    template <typename K, typename V, typename C, typename A>
    struct get_type_tag<std::map<K, V, C, A>> {
      using type = MapTag;
    };
    template <typename K, typename V, typename H, typename P, typename A>
    struct get_type_tag<std::unordered_map<K, V, H, P, A>> {
      using type = MapTag;
    };

    // Pair
    template <typename T1, typename T2> struct get_type_tag<std::pair<T1, T2>> {
      using type = PairTag;
    };

    // Tuples
    template <typename... Args> struct get_type_tag<std::tuple<Args...>> {
      using type = TupleTag;
    };

    // Optionals
    template <typename T> struct get_type_tag<std::optional<T>> {
      using type = OptionalTag;
    };

    // Smart Pointers
    template <typename T, typename D> struct get_type_tag<std::unique_ptr<T, D>> {
      using type = SmartPtrTag;
    };
    template <typename T> struct get_type_tag<std::shared_ptr<T>> {
      using type = SmartPtrTag;
    };

    // Filesystem Path
    template <> struct get_type_tag<std::filesystem::path> {
      using type = PathTag;
    };

    template <typename T>
    using get_type_tag_t = typename get_type_tag<typename std::decay<T>::type>::type;

  } // namespace detail

  // Main formatter struct template
  template <typename T> struct Formatter {
    static std::string to_string(const T &value) {
      using Tag = detail::get_type_tag_t<T>;

      if constexpr (std::is_same_v<Tag, detail::SequenceTag>) {
        return format_container(value, "[", "]");
      } else if constexpr (std::is_same_v<Tag, detail::SetTag>) {
        return format_container(value, "{", "}");
      } else if constexpr (std::is_same_v<Tag, detail::MapTag>) {
        return format_map(value);
      } else if constexpr (std::is_same_v<Tag, detail::PairTag>) {
        return format_pair(value);
      } else if constexpr (std::is_same_v<Tag, detail::TupleTag>) {
        return format_tuple(value);
      } else if constexpr (std::is_same_v<Tag, detail::OptionalTag>) {
        return format_optional(value);
      } else if constexpr (std::is_same_v<Tag, detail::SmartPtrTag>) {
        return format_ptr(value);
      } else if constexpr (std::is_same_v<Tag, detail::PathTag>) {
        return "\"" + value.string() + "\"";
      } else if constexpr (detail::is_string_type<typename std::decay<T>::type>::value) {
        if constexpr (std::is_same_v<typename std::decay<T>::type, std::string>) {
          return value;
        } else if constexpr (std::is_same_v<typename std::decay<T>::type, std::string_view>) {
          return std::string(value);
        } else {
          return value ? value : "(null)";
        }
      } else if constexpr (std::is_arithmetic_v<typename std::decay<T>::type>) {
        return std::to_string(value);
      } else {
        // Fallback for types capable of stream output
        std::ostringstream oss;
        oss << value;
        return oss.str();
      }
    }

  private:
    template <typename Container>
    static std::string format_container(const Container &container, const char *open,
                                        const char *close) {
      std::ostringstream oss;
      oss << open;
      bool first = true;
      for (const auto &item : container) {
        if (!first)
          oss << ", ";
        oss << tlog::format(item); // Recursive call
        first = false;
      }
      oss << close;
      return oss.str();
    }

    template <typename Map> static std::string format_map(const Map &map) {
      std::ostringstream oss;
      oss << "{";
      bool first = true;
      for (const auto &pair : map) {
        if (!first)
          oss << ", ";
        oss << tlog::format(pair.first) << ": " << tlog::format(pair.second);
        first = false;
      }
      oss << "}";
      return oss.str();
    }

    template <typename Pair> static std::string format_pair(const Pair &pair) {
      std::ostringstream oss;
      oss << "(" << tlog::format(pair.first) << ", " << tlog::format(pair.second) << ")";
      return oss.str();
    }

    template <typename Optional> static std::string format_optional(const Optional &opt) {
      if (!opt)
        return "nullopt";
      return "opt(" + tlog::format(*opt) + ")";
    }

    template <typename Ptr> static std::string format_ptr(const Ptr &ptr) {
      if (!ptr)
        return "nullptr";
      std::ostringstream oss;
      oss << "@" << ptr.get();
      return oss.str();
    }

    template <typename Tuple> static std::string format_tuple(const Tuple &t) {
      std::ostringstream oss;
      oss << "(";
      std::apply(
          [&oss](const auto &...args) {
            size_t n = 0;
            ((oss << (n++ > 0 ? ", " : "") << tlog::format(args)), ...);
          },
          t);
      oss << ")";
      return oss.str();
    }
  };

  // Entry point implementation
  template <typename T> std::string format(const T &value) {
    return Formatter<T>::to_string(value);
  }

  // Helpers for backward compatibility and specific types (optional but good for consistency)
  template <typename T> std::string to_string(const T &value) { return format(value); }

} // namespace tlog

  // Macros
  #define TLOG_VEC(vec) tlog::format(vec).c_str()
  #define TLOG_LIST(list) tlog::format(list).c_str()
  #define TLOG_DEQUE(deque) tlog::format(deque).c_str()
  #define TLOG_SET(set) tlog::format(set).c_str()
  #define TLOG_MAP(map) tlog::format(map).c_str()
  #define TLOG_PAIR(pair) tlog::format(pair).c_str()
  #define TLOG_CONTAINER(container) tlog::format(container).c_str()

#endif // __cplusplus
#endif // TLOG_HELPER_HPP
