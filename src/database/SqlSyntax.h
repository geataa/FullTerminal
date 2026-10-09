#pragma once
#include <string>
#include <vector>
#include <cwctype>
#include <algorithm>

namespace ft::db {
enum class Syntax { Plain, Keyword, String, Comment, Number };
struct Span { size_t start,length;Syntax kind; };
inline std::vector<Span> HighlightSql(const std::wstring& sql){
    std::vector<Span> out;size_t i=0;
    const std::wstring keywords=L"|SELECT|FROM|WHERE|INSERT|INTO|VALUES|UPDATE|SET|DELETE|CREATE|ALTER|DROP|TABLE|VIEW|INDEX|DATABASE|SCHEMA|JOIN|LEFT|RIGHT|INNER|OUTER|ON|AS|AND|OR|NOT|NULL|IS|IN|LIKE|ILIKE|BETWEEN|ORDER|BY|ASC|DESC|GROUP|HAVING|LIMIT|OFFSET|FETCH|ROWS|ONLY|WITH|RECURSIVE|UNION|ALL|DISTINCT|CASE|WHEN|THEN|ELSE|END|BEGIN|COMMIT|ROLLBACK|SAVEPOINT|RETURNING|PRIMARY|KEY|FOREIGN|REFERENCES|CONSTRAINT|DEFAULT|CHECK|UNIQUE|EXISTS|IF|GRANT|REVOKE|EXPLAIN|ANALYZE|TRUNCATE|TRUE|FALSE|INTEGER|INT|BIGINT|TEXT|VARCHAR|NUMERIC|DECIMAL|REAL|BOOLEAN|DATE|TIMESTAMP|USE|GO|";
    auto word=[](wchar_t c){return iswalnum(c)||c==L'_'||c==L'$';};
    while(i<sql.size()){
        const auto start=i;auto kind=Syntax::Plain;const auto c=sql[i++];
        if(c==L'-'&&i<sql.size()&&sql[i]==L'-'){kind=Syntax::Comment;while(i<sql.size()&&sql[i]!=L'\n')++i;}
        else if(c==L'/'&&i<sql.size()&&sql[i]==L'*'){
            kind=Syntax::Comment;++i;int depth=1;while(i<sql.size()&&depth){if(i+1<sql.size()&&sql[i]==L'/'&&sql[i+1]==L'*'){++depth;i+=2;}else if(i+1<sql.size()&&sql[i]==L'*'&&sql[i+1]==L'/'){--depth;i+=2;}else ++i;}
        }else if(c==L'\''||c==L'"'||c==L'`'||c==L'['){
            kind=Syntax::String;const auto close=c==L'['?L']':c;
            while(i<sql.size()){if(sql[i++]==close){if(i<sql.size()&&sql[i]==close){++i;continue;}break;}}
        }else if(c==L'$'){
            auto end=sql.find(L'$',i);bool tag=end!=sql.npos;
            if(tag)for(size_t n=i;n<end;++n)if(!iswalnum(sql[n])&&sql[n]!=L'_'){tag=false;break;}
            if(tag){kind=Syntax::String;auto delimiter=sql.substr(start,end-start+1);auto close=sql.find(delimiter,end+1);i=close==sql.npos?sql.size():close+delimiter.size();}
        }else if(iswdigit(c)){kind=Syntax::Number;while(i<sql.size()&&(iswalnum(sql[i])||sql[i]==L'.'||sql[i]==L'_'))++i;}
        else if(iswalpha(c)||c==L'_'){
            while(i<sql.size()&&word(sql[i]))++i;auto text=sql.substr(start,i-start);for(auto& ch:text)ch=(wchar_t)towupper(ch);
            if(keywords.find(L"|"+text+L"|")!=keywords.npos)kind=Syntax::Keyword;
        }
        if(!out.empty()&&out.back().kind==kind&&out.back().start+out.back().length==start)out.back().length=i-out.back().start;
        else out.push_back({start,i-start,kind});
    }return out;
}
// Track explicit transaction commands without interpreting strings, quoted identifiers,
// comments or PostgreSQL dollar-quoted function bodies as client commands.
inline bool SqlTransactionState(const std::wstring& sql,bool previous,bool retainStarts=false){
    auto visible=sql;for(const auto& span:HighlightSql(sql))if(span.kind==Syntax::String||span.kind==Syntax::Comment)
        std::fill(visible.begin()+span.start,visible.begin()+span.start+span.length,L' ');
    std::vector<std::wstring> words;std::wstring word;
    auto apply=[&]{
        if(words.empty())return;
        if(words[0]==L"BEGIN"||(words[0]==L"START"&&words.size()>1&&words[1]==L"TRANSACTION")||words[0]==L"SAVEPOINT")previous=true;
        else if(!retainStarts&&(words[0]==L"COMMIT"||words[0]==L"END"||(words[0]==L"ROLLBACK"&&(words.size()<2||words[1]!=L"TO"))))previous=false;
        words.clear();
    };
    for(size_t i=0;i<=visible.size();++i){wchar_t c=i<visible.size()?visible[i]:L';';
        if(iswalnum(c)||c==L'_')word+=(wchar_t)towupper(c);
        else{if(!word.empty()){words.push_back(std::move(word));word.clear();}if(c==L';')apply();}
    }return previous;
}
} // namespace ft::db
