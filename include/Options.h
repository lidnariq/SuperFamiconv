#ifndef OPTIONS_H
#define OPTIONS_H

#include <getopt.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <string>
#include <iostream>
#include <sstream>
#include <map>
#include <set>
#include <vector>
#include <functional>
#include <type_traits>
#include <stdexcept>

class Options {
public:
    Options() : IndentFlag(2), IndentDescription(18), optval(256) {}

    template <typename T>
    void Add(T& var, char flag, std::string long_flag, std::string description = "", T default_val = T(), std::string group = "");
    void AddSwitch(bool& var, char flag, std::string long_flag, std::string description = "", bool default_val = false, std::string group = "");

    bool Parse(int argc, char **argv);

    std::string Usage();
    std::string Header;
    unsigned IndentFlag;
    unsigned IndentDescription;

private:
    std::set<std::string> long_flags;
    std::vector<struct option> options;
    std::map<int, std::function<void(std::string)>> setters;
    std::map<std::string, std::vector<std::string>> usage;

    int optval;
    std::string optstr;

    template <typename T>
    void set(T& var, std::string optarg);

    template <typename T>
    void add_entry(struct option& op, char flag, std::string long_flag, T& default_val, std::string description, std::string group);

    template <typename T>
    std::string get_default(T& default_val);

    int tty_width();
};

template <typename T>
inline void Options::Add(T& var, char flag, std::string long_flag, std::string description, T default_val, std::string group) {
    struct option op;
    this->add_entry(op, flag, long_flag, default_val, description, group);

    this->optstr += ":";
    op.has_arg = required_argument;
    var = default_val;

    this->setters[op.val] = std::bind(&Options::set<T>, this, std::ref(var), std::placeholders::_1);
    this->options.push_back(op);
}

inline void Options::AddSwitch(bool& var, char flag, std::string long_flag, std::string description, bool default_val, std::string group) {
    struct option op;
    this->add_entry(op, flag, long_flag, var, description, group);

    op.has_arg = optional_argument;
    var = default_val;

    this->setters[op.val] = [&var](std::string) {
        var = !var;
    };

    this->options.push_back(op);
}

inline bool Options::Parse(int argc, char** argv) {
    this->options.push_back({NULL, 0, NULL, 0});
    int ch;
    while ((ch = getopt_long(argc, argv, this->optstr.c_str(), &this->options[0], NULL)) != -1) {
        auto it = this->setters.find(ch);
        if (it != this->setters.end()) {
            it->second(optarg ? optarg : "");
        } else {
            return false;
        }
    }
    return true;
}

inline std::string Options::Usage() {
    std::stringstream ss;
    if (Header.size()) ss << Header;

    for (auto &it : this->usage) {
        if (it.first.size() && it.first.compare(std::string("_"))) ss << it.first << ":\n";
        for (auto& description : it.second) ss << description << '\n';
        ss << '\n';
    }
    return ss.str();
}


template <typename T>
inline void Options::add_entry(struct option& opt, char flag, std::string long_flag, T& default_val, std::string description, std::string group) {
    if (!flag && !long_flag.size()) return;

    if (flag) {
        if (this->setters.find((int)(flag)) != this->setters.end()) {
            std::stringstream ss;
            ss << "Duplicate flag '" << flag << "'";
            throw std::runtime_error(ss.str());
        }
        this->optstr += flag;
        opt.val = flag;
    } else {
        opt.val = this->optval++;
    }

    if (long_flag.size()) {
        if (this->long_flags.find(long_flag) != this->long_flags.end()) {
            std::stringstream ss;
            ss << "Duplicate long flag \"" << long_flag << "\"";
            throw std::runtime_error(ss.str());
        }
        opt.name = this->long_flags.insert(long_flag).first->c_str();
    }
    opt.flag = NULL;

    if (description.size()) {
        std::stringstream ss;
        ss << std::string(IndentFlag, ' ');

        if (flag) ss << "-" << flag << " ";
        if (long_flag.size()) ss << "--" << long_flag << " ";
        ss << std::string(ss.str().length() > IndentDescription ? 1 : IndentDescription - ss.str().length(), ' ');

        std::string desc(description);
        desc.append(get_default(default_val));

        unsigned width = tty_width();
        unsigned desc_pos = width - ss.str().length() > width * 0.3f ? (unsigned)ss.str().length() : IndentFlag + 2;
        unsigned column_width = width - desc_pos;
        if (desc_pos == IndentFlag + 2) ss << '\n';

        for (int i = 0; i < desc.size(); i += column_width) {
            while (desc.substr(i, 1) == std::string(" ")) ++i;
            if (i + column_width < desc.size()) {
                ss << desc.substr(i, column_width) << '\n' << std::string(desc_pos, ' ');
            } else {
                ss << desc.substr(i);
            }
        }
        
        this->usage[group].push_back(ss.str());
    }
}

template <typename T>
inline void Options::set(T& var, std::string optarg) {
    std::stringstream ss(optarg);
    ss >> var;
}

template <>
inline void Options::set<std::string>(std::string& var, std::string optarg) {
    var = optarg;
}


template <typename T>
inline std::string Options::get_default(T& default_val) {
    if (std::is_same<T, bool>::value) {
        return std::string(" <switch>");
    } else if (std::is_same<T, unsigned>::value) {
        if (default_val) {
            std::stringstream ss;
            ss << " <default: " << default_val << ">";
            return ss.str();
        } else {
            return std::string();
        }
    } else {
        std::stringstream ss;
        ss << " <default: " << default_val << ">";
        return ss.str();
    }
}

template <>
inline std::string Options::get_default(std::string& default_val) {
    std::stringstream ss;
    if (default_val.size()) ss << " <default: " << default_val << ">";
    return ss.str();
}


inline int Options::tty_width() {
#ifdef TIOCGSIZE
    struct ttysize ts;
    ioctl(STDIN_FILENO, TIOCGSIZE, &ts);
    return ts.ts_cols >= 40 ? ts.ts_cols : 80;
#elif defined(TIOCGWINSZ)
    struct winsize ts;
    ioctl(STDIN_FILENO, TIOCGWINSZ, &ts);
    return ts.ws_col >= 40 ? ts.ws_col : 80;
#endif
}

#endif //OPTIONS_H
