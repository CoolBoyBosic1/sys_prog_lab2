#include <iostream>
#include <string>
#include <vector>
#include <unordered_set>
#include <cctype>
using namespace std;

// -------- Token ----------
struct Token { string type, lexeme; size_t line, col; };

// ASCII-safe принтер: керівні -> \n,\r,\t; решта поза 0x20..0x7E -> '?'
static string ascii_clean(const string& s){
    string o; o.reserve(s.size()*2);
    for (unsigned char c : s){
        if (c=='\n')      o += "\\n";
        else if (c=='\r') o += "\\r";
        else if (c=='\t') o += "\\t";
        else if (c>=0x20 && c<=0x7E) o += char(c);
        else o += '?';
    }
    return o;
}
static void print_tokens(const vector<Token>& toks, const char* title){
    cout << "=== " << title << " ===\n";
    for (auto& t : toks){
        cout << "<" << t.line << ":" << t.col << ">\t[" << t.type << "]\t"
             << ascii_clean(t.lexeme) << "\n";
    }
    cout << "\n";
}

// -------- Lexer ----------
struct Lexer {
    const string src;
    size_t pos=0, line=1, col=1;

    unordered_set<string> kw = {
        "abstract","and","array","as","break","callable","case","catch","class","clone",
        "const","continue","declare","default","do","echo","else","elseif","enddeclare",
        "endfor","endforeach","endif","endswitch","endwhile","enum","extends","final",
        "finally","fn","for","foreach","function","global","goto","if","implements",
        "include","include_once","instanceof","insteadof","interface","isset","list",
        "match","namespace","new","or","print","private","protected","public","require",
        "require_once","return","static","switch","throw","trait","try","unset","use",
        "var","while","xor","yield","true","false","null"
    };

    explicit Lexer(const string& s):src(s){}

    bool eof() const { return pos>=src.size(); }
    char peek(size_t k=0) const { return (pos+k<src.size()? src[pos+k] : '\0'); }
    char get(){ char c=peek(); if (!eof()){ ++pos; if (c=='\n'){ ++line; col=1; } else ++col; } return c; }
    void advance(size_t n){ while(n--) get(); }

    static bool is_ident_start(char c){ return std::isalpha((unsigned char)c) || c=='_'; }
    static bool is_ident(char c){ return std::isalnum((unsigned char)c) || c=='_'; }
    static bool is_digit(char c){ return std::isdigit((unsigned char)c); }

    bool try_match_any(const vector<string>& ops, string& out){
        for (auto& op : ops){
            bool ok = true;
            for (size_t i=0;i<op.size();++i){
                if (peek(i)!=op[i]){ ok=false; break; }
            }
            if (ok){ out=op; return true; }
        }
        return false;
    }

    vector<Token> run(){
        vector<Token> t;

        // попередньо відсортовані оператори (довші наперед)
        const vector<string> OPS1 = {
            "?->","::","=>","->","===","!==","==","!=", "<=", ">=", "<=>","??=","??",
            "&&","||","**","<<",">>"
        };
        const vector<string> OPS2 = {
            "+=","-=","*=","/=","%=","&=","|=","^=",".=", "<<=",">>=","**="
        };
        const vector<string> OPS3 = { "=", "+","-","*","/","%","&","|","^","~","!","<",">","?",":",".","@" };
        const string PUNCT = "()[]{};,";

        while(!eof()){
            // 1) whitespace
            if (isspace((unsigned char)peek())){
                get();
                continue;
            }

            size_t start_line=line, start_col=col;

            // 2) PHP open tag <?php
            if (peek()=='<' && peek(1)=='?' && peek(2)=='p' && peek(3)=='h' && peek(4)=='p' &&
                !is_ident(peek(5)) ){
                advance(5);
                t.push_back({"PHP_TAG","<?php",start_line,start_col});
                continue;
            }
            // 3) close tag ?>
            if (peek()=='?' && peek(1)=='>'){
                advance(2);
                t.push_back({"PHP_TAG_CLOSE","?>",start_line,start_col});
                continue;
            }

            // 4) comments
            if (peek()=='/' && peek(1)=='/'){
                string s; s += get(); s += get();
                while(!eof() && peek()!='\n') s += get();
                t.push_back({"COMMENT",s,start_line,start_col});
                continue;
            }
            if (peek()=='#'){
                string s; s += get();
                while(!eof() && peek()!='\n') s += get();
                t.push_back({"COMMENT",s,start_line,start_col});
                continue;
            }
            if (peek()=='/' && peek(1)=='*'){
                string s; s += get(); s += get();
                while(!eof()){
                    char c=get(); s+=c;
                    if (c=='*' && peek()=='/'){ s+=get(); break; } // <-- FIX тут було ')'
                }
                t.push_back({"COMMENT",s,start_line,start_col});
                continue;
            }

            // 5) strings: ' " `
            if (peek()=='\'' || peek()=='"' || peek()=='`'){
                char q = get(); string s; s+=q;
                bool closed=false;
                while(!eof()){
                    char c=get(); s+=c;
                    if (c=='\\'){ if(!eof()){ s+=get(); } continue; }
                    if (c==q){ closed=true; break; }
                }
                if (!closed){
                    t.push_back({"ERROR_UNTERMINATED_STRING", s, start_line,start_col});
                } else {
                    t.push_back({"STRING", s, start_line,start_col});
                }
                continue;
            }

            // 6) numbers
            if (is_digit(peek()) || (peek()=='.' && is_digit(peek(1)))){
                string s; bool is_float=false;
                if (peek()=='0' && (peek(1)=='x'||peek(1)=='X')){
                    s+=get(); s+=get();
                    while (isxdigit((unsigned char)peek()) || peek()=='_') s+=get();
                } else if (peek()=='0' && (peek(1)=='b'||peek(1)=='B')){
                    s+=get(); s+=get();
                    while (peek()=='0'||peek()=='1'||peek()=='_') s+=get();
                } else if (peek()=='0' && (peek(1)=='o'||peek(1)=='O')){
                    s+=get(); s+=get();
                    while ((peek()>='0'&&peek()<='7') || peek()=='_') s+=get();
                } else {
                    if (peek()=='.'){ s+=get(); is_float=true; while(is_digit(peek())||peek()=='_') s+=get(); }
                    else { while (is_digit(peek())||peek()=='_') s+=get(); }
                    if (peek()=='.' && is_digit(peek(1))){ is_float=true; s+=get(); while(is_digit(peek())||peek()=='_') s+=get(); }
                    if (peek()=='e'||peek()=='E'){ is_float=true; s+=get(); if (peek()=='+'||peek()=='-') s+=get(); while(is_digit(peek())||peek()=='_') s+=get(); }
                }
                (void)is_float;
                t.push_back({"NUMBER", s, start_line,start_col});
                continue;
            }

            // 7) variable
            if (peek()=='$' && is_ident_start(peek(1))){
                string s; s+=get();
                while (is_ident(peek())) s+=get();
                t.push_back({"VARIABLE", s, start_line,start_col});
                continue;
            }

            // 8) identifier/keyword
            if (is_ident_start(peek())){
                string s; s+=get();
                while (is_ident(peek())) s+=get();
                string type = (kw.count(s)? "KEYWORD":"IDENT");
                t.push_back({type, s, start_line,start_col});
                continue;
            }

            // 9) operators (довгі спочатку)
            {
                string op;
                if (try_match_any(OPS1, op)){ advance(op.size()); t.push_back({"OPERATOR",op,start_line,start_col}); continue; }
                if (try_match_any(OPS2, op)){ advance(op.size()); t.push_back({"OPERATOR",op,start_line,start_col}); continue; }
                if (try_match_any(OPS3, op)){ advance(op.size()); t.push_back({"OPERATOR",op,start_line,start_col}); continue; }
            }

            // 10) punctuation
            if (PUNCT.find(peek())!=string::npos){
                string s(1, get());
                t.push_back({"PUNCT", s, start_line,start_col});
                continue;
            }

            // 11) unknown char
            string s(1, get());
            t.push_back({"ERROR_UNKNOWN_CHAR", s, start_line,start_col});
        }

        return t;
    }
};

// -------- Тести (ASCII only) --------
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
$c = $b ??= 42;
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
$s2 = '\'';               // backslash + quote
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

int main(){
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    Lexer L1(TEST1), L2(TEST2);
    auto t1 = L1.run();
    auto t2 = L2.run();

    print_tokens(t1, "TEST1");
    print_tokens(t2, "TEST2");
    return 0;
}
