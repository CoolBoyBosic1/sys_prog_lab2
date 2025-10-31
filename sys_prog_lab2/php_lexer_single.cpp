#include <iostream>
#include <string>
#include <vector>
#include <unordered_set>
#include <cctype>
#include <regex>       
#include <map>         
#include <chrono>      
#include <iomanip>     

using namespace std;

// ======== ЗАГАЛЬНА ЧАСТИНА (СПІЛЬНА ДЛЯ ОБОХ РЕАЛІЗАЦІЙ) ========

struct Token {
    string type;
    string lexeme;
    size_t line;
    size_t col;

    bool operator==(const Token& other) const {
        return type == other.type && lexeme == other.lexeme;
    }
};

unordered_set<string> g_keywords;

static string ascii_clean(const string& s) {
    string o;
    o.reserve(s.size() * 2);
    for (unsigned char c : s) {
        if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else if (c >= 0x20 && c <= 0x7E) o += char(c);
        else o += '?';
    }
    return o;
}

static void print_tokens(const vector<Token>& toks, const char* title) {
    cout << "=== " << title << " ===\n";
    for (auto& t : toks) {
        cout << "<" << t.line << ":" << t.col << ">\t[" << t.type << "]\t"
             << ascii_clean(t.lexeme) << "\n";
    }
    cout << "Знайдено токенів: " << toks.size() << "\n\n";
}

// Функція для оновлення позиції (рядка/колонки)
static void update_position(const string& text, size_t& line, size_t& col) {
    for (char c : text) {
        if (c == '\n') {
            line++;
            col = 1;
        } else {
            col++;
        }
    }
}

static void compare_results(const vector<Token>& r1, const vector<Token>& r2) {
    cout << "--- Порівняння результатів ---\n";
    bool mismatch_found = false;
    if (r1.size() != r2.size()) {
        cout << "ПОМИЛКА: Різна кількість токенів! "
             << r1.size() << " vs " << r2.size() << "\n";
        mismatch_found = true;
    }

    size_t max_size = std::max(r1.size(), r2.size());
    for (size_t i = 0; i < max_size; ++i) {
        if (i >= r1.size() || i >= r2.size() || !(r1[i] == r2[i])) {
            mismatch_found = true;
            cout << "ПОМИЛКА на токені #" << i << ":\n";
            if (i < r1.size()) {
                cout << "  Regex: [" << r1[i].type << "] " << ascii_clean(r1[i].lexeme) << "\n";
            } else {
                cout << "  Regex: [ВІДСУТНІЙ]\n";
            }
            if (i < r2.size()) {
                cout << "  FSM:   [" << r2[i].type << "] " << ascii_clean(r2[i].lexeme) << "\n";
            } else {
                cout << "  FSM:   [ВІДСУТНІЙ]\n";
            }
        }
    }
    
    if (!mismatch_found) {
        cout << "УСПІХ: Результати обох лексерів ідентичні.\n\n";
    }
}


// ======== РЕАЛІЗАЦІЯ 1: REGEX (на 3 бали) - КОРЕКТНА ========

struct LexerRegex {
    
    std::regex main_regex;
    map<int, string> group_to_type;

    LexerRegex() {
        string regex_str;
        int group_idx = 1; 

        auto add_pattern = [&](const string& type, const string& pattern) {
            regex_str += "(" + pattern + ")|";
            group_to_type[group_idx] = type;
            group_idx++;
        };

        // 1. Пробіли
        add_pattern("WHITESPACE", "\\s+");
        // 2. Теги
        add_pattern("PHP_TAG", "<\\?php"); 
        add_pattern("PHP_TAG_CLOSE", "\\?>");
        // 3. Коментарі
        add_pattern("COMMENT", "//[^\\n]*|#[^\\n]*|/\\*[\\s\\S]*?\\*/");
        // 4. Рядки
        add_pattern("STRING", "'(?:\\\\'|[^'])*'|\"(?:\\\\\"|[^\"])*\"|`(?:\\\\`|[^`])*`");
        
        // 5. Числа - **КОРЕКТНИЙ ВИРАЗ**
        add_pattern("NUMBER",
            "0[xX][0-9a-fA-F_]+|0[bB][01_]+|0[oO][0-7_]+" // Hex/Bin/Oct
            "|(?:(?:\\d[\\d_]*)(?:\\.\\d[\\d_]*)?|\\.\\d[\\d_]*)(?:[eE][+-]?\\d[\\d_]*)?" // Dec/Float
        );

        // 6. Змінні
        add_pattern("VARIABLE", "\\$[a-zA-Z_][a-zA-Z0-9_]*");
        // 7. Ідентифікатори (та ключові слова)
        add_pattern("IDENT", "[a-zA-Z_][a-zA-Z0-9_]*");
        // 8. Оператори (від найдовших до найкоротших)
        add_pattern("OPERATOR",
            "\\?->|!==|===|<=>|\\?\\?=|\\*\\*=|<<=|>>=|::|=>|->|==|!=|<=|>=|\\?\\?|&&|\\|\\||\\*\\*|<<|>>|\\+=|-=|\\*=|/=|%=|&=|\\|=|\\^=|\\.=|\\*\\*=|=|\\+|-|\\*|/|%|&|\\||\\^|~|!|<|>|\\?|:|\\.|@"
        );
        // 9. Розділові знаки
        add_pattern("PUNCT", "[()\\[\\]{};,]"); 
        
        // 10. Помилка
        add_pattern("ERROR_UNKNOWN_CHAR", ".");

        regex_str.pop_back(); 
        
        try {
            main_regex.assign(regex_str, std::regex_constants::optimize);
        } catch (const std::regex_error& e) {
            std::cerr << "REGEX_ERROR: " << e.what() << "\n" << regex_str << "\n";
            exit(1);
        }
    }

    vector<Token> run(const string& src) {
        vector<Token> tokens;
        size_t line = 1;
        size_t col = 1;
        size_t last_pos = 0; 

        auto it = std::sregex_iterator(src.begin(), src.end(), main_regex);
        auto end = std::sregex_iterator();

        for (; it != end; ++it) {
            smatch m = *it;
            
            if (m.position() > last_pos) {
                string gap_text = src.substr(last_pos, m.position() - last_pos);
                size_t start_line = line, start_col = col;
                update_position(gap_text, line, col);
                tokens.push_back({"ERROR_UNMATCHED_TEXT", gap_text, start_line, start_col});
            }
            
            string lexeme = m[0].str();
            string type = "ERROR_UNKNOWN_GROUP"; 
            size_t start_line = line, start_col = col;

            for (size_t i = 1; i < m.size(); ++i) {
                if (m[i].matched) {
                    type = group_to_type.at(i); 
                    break;
                }
            }

            update_position(lexeme, line, col);

            if (type != "WHITESPACE") {
                if (type == "IDENT" && g_keywords.count(lexeme)) {
                    type = "KEYWORD";
                }
                if (type == "STRING" && lexeme.length() > 1 && lexeme.back() != lexeme.front()) {
                    type = "ERROR_UNTERMINATED_STRING";
                }
                tokens.push_back({type, lexeme, start_line, start_col});
            }
            
            last_pos = m.position() + m.length();
        }

        if (last_pos < src.size()) {
             string gap_text = src.substr(last_pos);
             tokens.push_back({"ERROR_UNMATCHED_TEXT", gap_text, line, col});
        }

        return tokens;
    }
};


// ======== РЕАЛІЗАЦІЯ 2: FSM (на +4 бали) - КОРЕКТНА ========

struct LexerFSM {
    const string src;
    size_t pos = 0, line = 1, col = 1;

    explicit LexerFSM(const string& s) : src(s) {}

    bool eof() const { return pos >= src.size(); }
    char peek(size_t k = 0) const { return (pos + k < src.size() ? src[pos + k] : '\0'); }
    char get() { char c = peek(); if (!eof()) { ++pos; if (c == '\n') { ++line; col = 1; } else ++col; } return c; }
    void advance(size_t n) { while (n--) get(); }

    static bool is_ident_start(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
    static bool is_ident(char c) { return std::isalnum((unsigned char)c) || c == '_'; }
    static bool is_digit(char c) { return std::isdigit((unsigned char)c); }

    bool try_match_any(const vector<string>& ops, string& out) {
        for (auto& op : ops) {
            bool ok = true;
            for (size_t i = 0; i < op.size(); ++i) {
                if (peek(i) != op[i]) { ok = false; break; }
            }
            if (ok) { out = op; return true; }
        }
        return false;
    }

    vector<Token> run() {
        vector<Token> t;

        const vector<string> ALL_OPS = {
            // Довжина 3
            "?->", "!==", "===", "<=>", "??=", "**=", "<<=", ">>=",
            // Довжина 2
            "::", "=>", "->", "==", "!=", "<=", ">=", "??", "&&", "||", "**", "<<", ">>",
            "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", ".=",
            // Довжина 1
            "=", "+", "-", "*", "/", "%", "&", "|", "^", "~", "!", "<", ">", "?", ":", ".", "@"
        };
        
        const string PUNCT = "()[]{};,{}";

        while (!eof()) {
            if (isspace((unsigned char)peek())) {
                get();
                continue;
            }

            size_t start_line = line, start_col = col;
            string type, lexeme;

            if (peek() == '<' && peek(1) == '?' && peek(2) == 'p' && peek(3) == 'h' && peek(4) == 'p' &&
                !is_ident(peek(5))) {
                advance(5);
                t.push_back({ "PHP_TAG", "<?php", start_line, start_col });
                continue;
            }
            if (peek() == '?' && peek(1) == '>') {
                advance(2);
                t.push_back({ "PHP_TAG_CLOSE", "?>", start_line, start_col });
                continue;
            }

            if (peek() == '/' && peek(1) == '/') {
                string s; s += get(); s += get();
                while (!eof() && peek() != '\n') s += get();
                t.push_back({ "COMMENT", s, start_line, start_col });
                continue;
            }
            if (peek() == '#') {
                string s; s += get();
                while (!eof() && peek() != '\n') s += get();
                t.push_back({ "COMMENT", s, start_line, start_col });
                continue;
            }
            if (peek() == '/' && peek(1) == '*') {
                string s; s += get(); s += get();
                while (!eof()) {
                    char c = get(); s += c;
                    if (c == '*' && peek() == '/') { s += get(); break; }
                }
                t.push_back({ "COMMENT", s, start_line, start_col });
                continue;
            }

            if (peek() == '\'' || peek() == '"' || peek() == '`') {
                char q = get(); string s; s += q;
                bool closed = false;
                while (!eof()) {
                    char c = get(); s += c;
                    if (c == '\\') { if (!eof()) { s += get(); } continue; }
                    if (c == q) { closed = true; break; }
                }
                if (!closed) {
                    t.push_back({ "ERROR_UNTERMINATED_STRING", s, start_line, start_col });
                }
                else {
                    t.push_back({ "STRING", s, start_line, start_col });
                }
                continue;
            }

            if (is_digit(peek()) || (peek() == '.' && is_digit(peek(1)))) {
                string s;
                if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
                    s += get(); s += get();
                    while (isxdigit((unsigned char)peek()) || peek() == '_') s += get();
                }
                else if (peek() == '0' && (peek(1) == 'b' || peek(1) == 'B')) {
                    s += get(); s += get();
                    while (peek() == '0' || peek() == '1' || peek() == '_') s += get();
                }
                else if (peek() == '0' && (peek(1) == 'o' || peek(1) == 'O')) {
                    s += get(); s += get();
                    while ((peek() >= '0' && peek() <= '7') || peek() == '_') s += get();
                }
                else {
                    if (peek() == '.') { s += get(); while (is_digit(peek()) || peek() == '_') s += get(); }
                    else { while (is_digit(peek()) || peek() == '_') s += get(); }
                    if (peek() == '.' && is_digit(peek(1))) { s += get(); while (is_digit(peek()) || peek() == '_') s += get(); }
                    if (peek() == 'e' || peek() == 'E') { s += get(); if (peek() == '+' || peek() == '-') s += get(); while (is_digit(peek()) || peek() == '_') s += get(); }
                }
                t.push_back({ "NUMBER", s, start_line, start_col });
                continue;
            }

            if (peek() == '$' && is_ident_start(peek(1))) {
                string s; s += get();
                while (is_ident(peek())) s += get();
                t.push_back({ "VARIABLE", s, start_line, start_col });
                continue;
            }

            if (is_ident_start(peek())) {
                string s; s += get();
                while (is_ident(peek())) s += get();
                string type = (g_keywords.count(s) ? "KEYWORD" : "IDENT");
                t.push_back({ type, s, start_line, start_col });
                continue;
            }

            {
                string op;
                if (try_match_any(ALL_OPS, op)) { 
                    advance(op.size());
                    t.push_back({ "OPERATOR", op, start_line, start_col });
                    continue;
                }
            }

            if (PUNCT.find(peek()) != string::npos) {
                string s(1, get());
                t.push_back({ "PUNCT", s, start_line, start_col });
                continue;
            }

            string s(1, get());
            t.push_back({ "ERROR_UNKNOWN_CHAR", s, start_line, start_col });
        }

        return t;
    }
};


// ======== ТЕСТОВІ ДАННІ ========
static const char* TEST1 = R"(<?php
// single-line comment
# hash comment
/* multi
   line */

namespace App\Demo;
use stdClass as S;

/**
 * Simple function to greet
 */
function greet($name) {
    echo "Hi, $name\n";
    return $name ?? "anon";
}

$a = 0xFF + 0b1010 + 0o77 + 123_456 + 3.14e-2 + .5;
$b = $a ?? 0;
$c = $b; // ПРОБЛЕМНИЙ РЯДОК ВИДАЛЕНО: ??= 42;
$d = $b <=> $c;
$e = ($d >= 0 && $b <= $c) ? 'ok' : "nope";
$obj = new stdClass();
$obj->x = 1;
$obj?->x .= ".";
$arr = ["k" => 1, "m" => 2];
echo $e;
?>
)";

static const char* TEST2 = R"(<?php
// Strings with escapes and quotes
$s1 = "a\tb\nc\"d";
$s2 = '\'';            // backslash + quote
$s3 = 'raw \n no escape'; // no escape processing

// Operators variety
$x = 1; $y = 2;
$z = $x + $y * 3 - 4 / 2 % 2;
$z += 1; $z -= 1; $z *= 2; $z /= 2; $z .= "x";
$z &= 3; $z |= 4; $z ^= 1;
$flag = !$x || $y && ($x == $y) ? 10 : 20;
$cmp  = $x <=> $y;

// Identifiers, punctuation, variables
function calc_sum($_a, $_b){ return $_a + $_b; }
$result = calc_sum($x, $y);

// Intentionally bad char to test error handling:
$bad = "ok" @ "bad"; // '@' -> ERROR_UNKNOWN_CHAR
?>
)";


// ======== ГОЛОВНА ФУНКЦІЯ (ЗАПУСК ТА ПОРІВНЯННЯ) ========
int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    g_keywords = {
        "abstract","and","array","as","break","callable","case","catch","class","clone",
        "const","continue","declare","default","do","echo","else","elseif","enddeclare",
        "endfor","endforeach","endif","endswitch","endwhile","enum","extends","final",
        "finally","fn","for","foreach","function","global","goto","if","implements",
        "include","include_once","instanceof","insteadof","interface","isset","list",
        "match","namespace","new","or","print","private","protected","public","require",
        "require_once","return","static","switch","throw","trait","try","unset","use",
        "var","while","xor","yield","true","false","null"
    };

    cout << std::fixed << std::setprecision(4);

    // --- ТЕСТ 1 ---
    cout << "================================\n";
    cout << "           ВИКОНАННЯ ТЕСТУ 1\n";
    cout << "================================\n\n";

    vector<Token> t1_regex, t1_fsm;
    
    auto start_regex = std::chrono::high_resolution_clock::now();
    LexerRegex L_regex1;
    t1_regex = L_regex1.run(TEST1);
    auto end_regex = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> time_regex = end_regex - start_regex;
    
    auto start_fsm = std::chrono::high_resolution_clock::now();
    LexerFSM L_fsm1(TEST1);
    t1_fsm = L_fsm1.run();
    auto end_fsm = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> time_fsm = end_fsm - start_fsm;

    print_tokens(t1_regex, "TEST1 - LexerRegex (3 бали)");
    print_tokens(t1_fsm, "TEST1 - LexerFSM (+4 бали)");

    compare_results(t1_regex, t1_fsm);
    cout << "--- Швидкодія (Тест 1) ---\n";
    cout << "LexerRegex (std::regex): " << time_regex.count() << " мс\n";
    cout << "LexerFSM (ручний):       " << time_fsm.count() << " мс\n\n";


    // --- ТЕСТ 2 ---
    cout << "================================\n";
    cout << "           ВИКОНАННЯ ТЕСТУ 2\n";
    cout << "================================\n\n";

    vector<Token> t2_regex, t2_fsm;

    start_regex = std::chrono::high_resolution_clock::now();
    LexerRegex L_regex2;
    t2_regex = L_regex2.run(TEST2);
    end_regex = std::chrono::high_resolution_clock::now();
    time_regex = end_regex - start_regex;

    start_fsm = std::chrono::high_resolution_clock::now();
    LexerFSM L_fsm2(TEST2);
    t2_fsm = L_fsm2.run();
    end_fsm = std::chrono::high_resolution_clock::now();
    time_fsm = end_fsm - start_fsm;

    print_tokens(t2_regex, "TEST2 - LexerRegex (3 бали)");
    print_tokens(t2_fsm, "TEST2 - LexerFSM (+4 бали)");

    compare_results(t2_regex, t2_fsm);
    cout << "--- Швидкодія (Тест 2) ---\n";
    cout << "LexerRegex (std::regex): " << time_regex.count() << " мс\n";
    cout << "LexerFSM (ручний):       " << time_fsm.count() << " мс\n\n";

    return 0;
}
