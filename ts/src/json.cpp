#include <memory>
#include <set>
#include <ts/json.h>
#include <ts/string.h>
#include <ts/log.h>

_TS_NAMESPACE_USING

_TS_NAMESPACE_BEGIN

#define isslash(_c_)                (_c_ == '/' || _c_ == '\\')
#define skip_slash(_token_)         while(p < e && isslash(*p)){p++;} _token_ = p;
#define scan_name(_token_, _len_)   skip_slash(_token_); _len_ = 0; while(p < e && !isslash(*p)){p++; _len_++;}

#define isblank(_c_)                (_c_ == '\n' || _c_ == '\r' || _c_ == '\t' || _c_ == ' ')
#define isstring(_c_)               (_c_ == '\'' || _c_ == '\"')
#define issep(_c_)                  (_c_ == ',')

#if defined(_MSC_VER) || defined(ANDROID) || defined(_OS_LINUX_)
# define ishexnumber(_c_)            (_c_ == 'x' || (_c_ >= '0' && _c_ <= '9') || (_c_ >= 'a' && _c_ <= 'f')  || (_c_ >= 'A' && _c_ <= 'F') )
#endif

#define scan_blank(_token_)         while(p < e && isblank(*p)){if (*p == '\n'){line++;} p++;} _token_ = p;
#define scan_token(_token_)         scan_blank(_token_);
#define scan_id(_token_, _len_)     scan_blank(_token_); _len_ = 0; while(p < e && isalnum(*p)){p++; _len_++;}
#define scan_string(_token_, _len_) scan_string_c(_token_, _len_, p, e, line) //scan_blank(_token_); _len_ = 2; {char _ew = *p++; while(p < e && (*p != _ew || *(p-1) == '\\')){if (*p == '\n' && *(p-1) == '\\'){line++;} p++; _len_++;} p++;}
#define scan_ior(_token_, _len_, _has_dot_, _has_hex)    \
scan_blank(_token_); _len_ = 0; \
{bool _h1 = false, _h2 = false; _has_hex = 0; _has_dot_ = 0; if (*p == '-' || *p == '+') {p++; _len_++;} \
while(p < e && (isdigit(*p) || (_h1 = (*p == '.')) || (_h2 = ishexnumber(*p)))){p++; _len_++; _has_dot_ |= _h1; _has_hex |= _h2; }}

static std::set<std::string> jsKeywords = {"null", "undefined", "true", "false"};

namespace json {
    void scan_string_c(const char*& _token_, int& _len_, const char*& p, const char*& e, int& line) {
        scan_blank(_token_); _len_ = 2;
        char _ew = *p++;
        while(p < e && *p != _ew) {
            if (*p == '\n'){line++;}
            else if (*p == '\\') {
                p++; _len_++;
                if (p >= e) break;
            }
            p++; _len_++;
        }
        p++;
    }
    
    bool skip_unmeaning(std::string& err, const char*& ptr, int len, int& line) {
        const char* e = ptr + len;
        while (ptr < e) {
            if (isblank(*ptr)) {
                if (*ptr == '\n') {
                    line++;
                }
                ptr++;
            }
            else if (*ptr == '/' && *(ptr + 1) == '/') { //line comment
                ptr += 2;
                while (ptr < e && *ptr != '\n') {
                    ptr++;
                    line++;
                }
            }
            else if (*ptr == '/' && *(ptr + 1) == '*') { //comment block
                ptr += 2;
                const char* comment = ptr;
                while (ptr < e) {
                    while (ptr < e && *ptr != '/') {
                        if (*ptr == '\n') {line++;}
                        ptr++;
                    }
                    if (ptr > comment && *(ptr - 1) == '*') {
                        ptr++;
                        break;
                    }
                }
                if (*ptr == 0 && (*(ptr - 1) != '/' || *(ptr - 2) != '*')) {
                    ts::string::format(err, "Unterminated /* comment nearby %.64s...!", ptr - 2);
                    return false;
                }
            }
            else {
                break;
            }
        }
        return true;
    }
    
    void skip_until_commit(std::string& err, const char*& ptr, int len, int& line, const std::pair<char, char>& parantheses) {
        const char* e = ptr + len;
        while (ptr < e) {
            if (isblank(*ptr)) {
                if (*ptr == '\n') {
                    line++;
                }
                ptr++;
            }
            else if (*ptr == '/' && *(ptr + 1) == '/') { //line comment
                break;
            }
            else if (*ptr == '/' && *(ptr + 1) == '*') { //comment block
                break;
            }
            else if (*ptr == parantheses.first || *ptr == parantheses.second) {
                break;
            }
            else {
                ptr++;
            }
        }
    }
    
    bool skip_parantheses(std::string& err, const char*& ptr, int len, int& line, const std::pair<char, char>& parantheses) {
        const char* e = ptr + len;
        if (*ptr != parantheses.first) {
            ts::string::format(err, "exptected { but found '%1s' nearby %.64s...!", ptr, ptr);
            return false;
        }
        ptr++;
        int count = 1;
        while (ptr < e) {
            if (!skip_unmeaning(err, ptr, len, line)) {
                return -1;
            }
            skip_until_commit(err, ptr, len, line, parantheses);
            if (*ptr == parantheses.first) {
                count++;
                ptr++;
            }
            else if (*ptr == parantheses.second) {
                count--;
                ptr++;
                if (count == 0) { //end
                    break;
                }
            }
        }
        if (count) {
            ts::string::format(err, "Unterminated { nearby %.64s...!", ptr - 2);
            return false;
        }
        return true;
    }
    
    bool skip_big_parantheses(std::string& err, const char*& ptr, int len, int& line) {
        static std::pair<char, char> setParantheses {'{','}'};
        return skip_parantheses(err, ptr, len, line, setParantheses);
    }
    
    bool skip_in_parantheses(std::string& err, const char*& ptr, int len, int& line) {
        static std::pair<char, char> setParantheses {'[',']'};
        return skip_parantheses(err, ptr, len, line, setParantheses);
    }

    bool skip_small_parantheses(std::string& err, const char*& ptr, int len, int& line) {
        static std::pair<char, char> setParantheses {'(',')'};
        return skip_parantheses(err, ptr, len, line, setParantheses);
    }

    bool    parserMap(const char* src, int len, std::map<std::string, ts::pie>& values, int& readx, int& line, std::string& err);
    bool    parserValue(const char* src, int len, ts::pie& value, int& readx, int& line, std::string& err) {
        const char* p = src;
        const char* e = src + ((len != -1) ? len : len = (int)strlen(src));
        const char* tk = nullptr;
        
        while (p < e) {
            if (!skip_unmeaning(err, p, len, line)) {
                return false;
            }
            scan_token(tk);
            if (p >= e) {
                /*error*/
                err = "illegal json source!";
                return false;
            }
            if (*p == '/' && *(p + 1) == '*') { //is commentary
                p += 2;
                const char* pp = strstr(p, "*/");
                if (pp == nullptr) {
                    return false;
                }
                p = pp + 2;
                continue;
            }
            switch (*tk) {
                case '{': { /*map*/
                    int rx = 0;
                    if (value.isMap() == false) {
                        value = std::map<std::string, ts::pie>{};
                    }
                    if (parserMap(p, len - (int)(p - src), value.map(), rx, line, err) == false) {
                        /*error*/
                        return false;
                    }
                    
                    p += rx;
                    
                    readx = (int)(p - src);
                    
                    return true;
                }break;
                    
                case '[': { /*list*/
                    p++;

                    if (value.isArray() == false) {
                        value = std::vector<ts::pie>{};
                    }

                    if (!skip_unmeaning(err, p, len, line)) {
                        return false;
                    }
                    scan_token(tk);
                    if (*tk == ']') { /*empty list*/
                        p++;
                        readx = (int)(p - src);
                        return true;
                    }
                    
                    while (p < e) {
                        int rx = 0;
                        ts::pie& sub = *value.array().insert(value.array().end(), ts::pie());
                        if (parserValue(p, len - (int)(p - src), sub, rx, line, err) == false) {
                            /*error*/
                            return false;
                        }
                        
                        p += rx;
                        
                        if (!skip_unmeaning(err, p, len, line)) {
                            return false;
                        }
                        scan_token(tk);
                        
                        if (*tk == ']') {
                            p++;
                            break;
                        }
                        else if (*tk != ',') {
                            /*warning*/
                            p++;
                        }
                        else { /*again*/
                            p++;
                            if (!skip_unmeaning(err, p, len, line)) {
                                return false;
                            }
                            scan_token(tk);
                            if (*tk == ']') {
                                p++;
                                break;
                            }
                        }
                    }
                    
                    readx = (int)(p - src);
                    
                    return true;
                }break;
                    
                default: {
                    const char* val = nullptr;
                    int vallen = 0;
                    
                    if (!skip_unmeaning(err, p, len, line)) {
                        return false;
                    }
                    if (isstring(*tk)) {
                        scan_string(val, vallen);
                        if (vallen <= 0 || *val != *(val + vallen - 1)) {
                            /*error*/
                            ts::string::format(err, "illegal string : nearby %.64s!", val);
                            return false;
                        }
                        value = std::string(val + 1, vallen - 2);
                        readx = (int)(p - src);
                        return true;
                    }
                    else {
                        bool has_dot = false, has_hex = false;
                        const char* p_save = p;
                        scan_ior(val, vallen, has_dot, has_hex);
                        if (vallen <= 0 || (*p != ' ' && *p != ',' && *p != ']' && *p != '}' && *p != '\r' && *p != '\n')) { //deal as string
                            p = p_save;
                            scan_blank(val); vallen = 0;
                            
                            const char* tmp = nullptr;
                            
                            while (p < e && *p != '('  && *p != ' ' && *p != ',' && *p != ']' && *p != '}') {
                                if (*p == '\n') {
                                    line++;
                                    if (vallen == 0) {
                                        vallen = int (p - val);
                                        if (*(p - 1) == '\r') {
                                            vallen--;
                                        }
                                    }
                                }
                                p++;
                            }
                            if (vallen == 0) {vallen = int (p - val);}

                            if (*p == ' ') {
                                scan_blank(tmp);
                            }

                            bool isJsFun = false;
                            if (*p == '(') { //is js function
                                if (skip_small_parantheses(err, p, len - (int)(p - src), line) == false) {
                                    return false;
                                }
                                const char* p_fun_body = nullptr;
                                scan_blank(p_fun_body);
                                if (skip_big_parantheses(err, p, len - (int)(p - src), line) == false) {
                                    return false;
                                }
                                isJsFun = true;
                                vallen = int (p - val);
                            }
                            value = std::string(val, vallen);
                            if (isJsFun) {
                                value._flags |= flags_is_jscode | flags_is_jsfunction;
                            }
                            else if (jsKeywords.find(value.get<std::string>()) != jsKeywords.end()) {
                                value._flags |= flags_is_jscode;
                                if (strncmp(val, "true", vallen) == 0 || strncmp(val, "false", vallen) == 0) {
                                    value._flags |= flags_is_boolean;
                                }
                            }
                            readx = (int)(p - src);
                            return true;
                        }
                        //is number
                        if (has_dot) {
                            value = atof(val);
                        }
                        else {
                            value = atoll(val);
                        }
                        readx = (int)(p - src);
                        return true;
                    }
                }break;
            }
        }
        
        return false;
    }
    
    bool    parserMap(const char* src, int len, std::map<std::string, ts::pie>& values, int& readx, int& line, std::string& err) {
        const char* p = src;
        const char* e = src + ((len != -1) ? len : len = (int)strlen(src));
        const char* tk = nullptr;
        
        if (!skip_unmeaning(err, p, len, line)) {
            return false;
        }
        scan_token(tk);
        if (p >= e || *tk != '{') {
            /*error*/
            err = "illegal json source!";
            return false;
        }
        
        { /*map*/
            p++;
            
            if (!skip_unmeaning(err, p, len, line)) {
                return false;
            }
            scan_token(tk);
            if (*tk == '}') { /*empty map*/
                p++;
                readx = (int)(p - src);
                return true;
            }
            
            while (p < e) {
                const char* sid = nullptr;
                int sidlen = 0;
                
                if (!skip_unmeaning(err, p, len, line)) {
                    return false;
                }
                scan_blank(sid); sidlen = 0;
                char endWith = (*sid == '\"' || *sid == '\'') ? *sid : ':';
                if (endWith == '\"' || endWith == '\'') {
                    p++;
                    sidlen++;
                    while(p < e && *p != endWith){p++; sidlen++;}
                }
                else {
                    while(p < e && !isblank(*p) && *p != endWith){p++; sidlen++;}
                }
                if (sidlen <= 0) {
                    /*error*/
                    err = "illegal id found!";
                    return false;
                }
                if (endWith == '\"' || endWith == '\'') {
                    p++;
                    sidlen++;
                }
                
                //remove "
                if ( (*sid == '\"' || *sid == '\'') && (*(sid + sidlen - 1) == *sid) ) {
                    sid++;
                    sidlen -= 2;
                }
                
                if (sidlen < 0) {
                    /*error*/
                    ts::string::format(err, "missing id: nearby %.64s!", sid);
                    return false;
                }
                
                if (!skip_unmeaning(err, p, len, line)) {
                    return false;
                }

                std::string sId(sid, sidlen);
                
                const char* comma = nullptr;
                scan_token(comma);
                
                p++;
                
                if (*comma != ':') {
                    /*error*/
                    ts::string::format(err, "illegal id: nearby %.64s!", p - 1);
                    return false;
                }
                
                ts::pie& value = values[sId];
                
                int rx = 0;
                if (parserValue(p, len - (int)(p - src), value, rx, line, err) == false) {
                    /*error*/
                    return false;
                }
                
                p += rx;
                
                if (!skip_unmeaning(err, p, len, line)) {
                    return false;
                }

                scan_token(tk);
                
                if (*tk == '}') {
                    p++;
                    break;
                }
                else if (*tk != ',') {
                    /*warning*/
                    p++;
                }
                else { /*again*/
                    p++;
                    if (!skip_unmeaning(err, p, len, line)) {
                        return false;
                    }
                    scan_token(tk);
                    if (*tk == '}') {
                        p++;
                        break;
                    }
                }
            }
            
            readx = (int)(p - src);
        }
        return true;
    }

    bool parse(ts::pie& out, const char* src, std::string& err) {
        int readx = 0, line = 1;
        return parserValue(src, (int)strlen(src), out, readx, line, err);
    }
    
    bool isIdentifier(const char* s) {
        if (!isalpha(*s) && *s != '$' && *s != '_') {
            return false;
        }
        while (*++s) {
            if (*s != '_' && !isalpha(*s) && !isdigit(*s)) {
                return false;
            }
        }
        return true;
    }
    
    std::string&    format(const ts::pie& js, std::string& out, bool quot, int align, int indent) {
        std::string sIndentPre((align ? (align - 1) : 0) * indent, ' ');
        std::string sIndent(align * indent, ' ');
        if (align) sIndentPre.insert(0, "\r\n");
        if (align) sIndent.insert(0, "\r\n");

        std::type_index idx = js.type();
        if (idx == typeid(int64_t)) {
            char zb[64];
            snprintf(zb, sizeof(zb), "%lld", js.get<int64_t>());
            out += zb;
        }
        else if (idx == typeid(double)) {
            char zb[64];
            snprintf(zb, sizeof(zb), "%f", js.get<double>());
            out += zb;
        }
        else if (idx == typeid(std::string)) {
            if (!(js._flags & (flags_is_jsfunction | flags_is_boolean | flags_is_jscode))) {
                out += "\"";
            }
            out += js.get<std::string>();
            if (!(js._flags & (flags_is_jsfunction | flags_is_boolean | flags_is_jscode))) {
                out += "\"";
            }
        }
        else if (idx == typeid(std::vector<ts::pie>)) {
            out += "[";
            bool hasObject = false;
            for (auto& it : js.array()) {
                if (it.isMap() || it.isArray()) { hasObject = true; break;}
            }
            for (auto& it : js.array()) {
                if (hasObject) {
                    out += sIndent;
                }
                format(it, out, quot, align ? align+1 : 0, indent);
                out += ",";
            }
            if (js.array().size()) {
                out.replace(out.length() - 1, 1, "");
            }
            if (hasObject) {
                out += sIndentPre;
            }
            out += "]";
        }
        else if (idx == typeid(std::map<std::string, ts::pie>)) {
            out += "{";
            bool hasObject = false;
            for (auto& it : js.map()) {
                if (it.second.isMap() || it.second.isArray()) { hasObject = true; break;}
            }
            for (auto& it : js.map()) {
                if (hasObject) {
                    out += sIndent;
                }
                bool isIds = isIdentifier(it.first.c_str());
                if (!isIds || quot)out += "\"";
                out += it.first;
                if (!isIds || quot)out += "\"";
                out += ":";
                if (indent) out += " ";
                format(it.second, out, quot, align+1, indent);
                out += ",";
            }
            if (js.map().size()) {
                out.replace(out.length() - 1, 1, "");
            }
            if (hasObject) {
                out += sIndentPre;
            }
            out += "}";
        }
        return out;
    }
    
    std::string&    format(const ts::pie& js, std::string& out, bool quot, bool align) {
        return format(js, out, quot, align, 2);
    }
    
    bool            fromFile(ts::pie& out, const char* file, std::string& err) {
        FILE* fp = fopen(file, "rb");
        if (fp == nullptr) {
            log_error("file[%s] not found!", file);
            return false;
        }
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        std::shared_ptr<char> szb(new char[sz + 2]);
        fread(szb.get(), sz, 1, fp);
        szb.get()[sz] = 0;
        fclose(fp);
        return parse(out, szb.get(), err);
    }
    
    long            toFile(const ts::pie& js, const char* file, bool quot, bool align) {
        std::string s;
        format(js, s, quot, align);
        if (s.size()) {
            FILE* fp = fopen(file, "wb");
            if (fp == nullptr) {
                log_warning("can't open file[%s]!", file);
            }
            else {
                fwrite(s.c_str(), s.size(), 1, fp);
                fclose(fp);
            }
        }
        return (long)s.size();
    }
};

_TS_NAMESPACE_END

