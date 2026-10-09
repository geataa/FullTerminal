#include "database/Database.h"
#include "core/Utf8.h"
#include "mcp/Json.h"
#include <algorithm>
#include <sstream>

namespace ft::db {
std::wstring EngineName(Engine engine) {
    switch (engine) {
    case Engine::SQLite: return L"SQLite";
    case Engine::PostgreSQL: return L"PostgreSQL";
    case Engine::MySQL: return L"MySQL / MariaDB";
    case Engine::SqlServer: return L"SQL Server";
    case Engine::Oracle: return L"Oracle";
    default: return L"ODBC / DSN";
    }
}
std::wstring QuoteIdentifier(Engine engine, const std::wstring& name) {
    const wchar_t start = engine == Engine::MySQL ? L'`' : engine == Engine::SqlServer ? L'[' : L'"';
    const wchar_t end = engine == Engine::SqlServer ? L']' : start;
    std::wstring out(1,start);
    for (auto c : name) { if (c == end) out += end; out += c; }
    return out + end;
}
std::wstring QualifiedName(Engine engine, const Table& table) {
    std::wstring out;
    if (!table.catalog.empty() && engine == Engine::SqlServer) out += QuoteIdentifier(engine,table.catalog)+L".";
    if (!table.schema.empty()) out += QuoteIdentifier(engine,table.schema)+L".";
    else if(engine==Engine::MySQL&&!table.catalog.empty()) out += QuoteIdentifier(engine,table.catalog)+L".";
    return out + QuoteIdentifier(engine,table.name);
}
std::wstring SelectTable(Engine e, const Table& table, const std::vector<Column>& columns,
                         int sort, bool desc, int offset, int limit, int filter, bool isNull) {
    std::wstring sql=L"SELECT * FROM "+QualifiedName(e,table);
    if (filter>=0 && filter<(int)columns.size())
        sql+=L" WHERE "+QuoteIdentifier(e,columns[filter].name)+(isNull?L" IS NULL":L" = ?");
    std::vector<std::wstring> order;
    if (sort>=0 && sort<(int)columns.size()) order.push_back(QuoteIdentifier(e,columns[sort].name)+(desc?L" DESC":L" ASC"));
    for (int i=0;i<(int)columns.size();++i) if (columns[i].primaryKey && i!=sort) order.push_back(QuoteIdentifier(e,columns[i].name));
    if (order.empty() && !columns.empty()) order.push_back(QuoteIdentifier(e,columns[0].name));
    if (!order.empty()) {
        sql+=L" ORDER BY "; for(size_t i=0;i<order.size();++i) { if(i)sql+=L", "; sql+=order[i]; }
    }
    offset=std::clamp(offset,0,10000000); limit=std::clamp(limit,1,10000);
    if(e==Engine::SqlServer || e==Engine::Oracle) {
        if(order.empty()) sql+=L" ORDER BY (SELECT NULL)";
        sql+=L" OFFSET "+std::to_wstring(offset)+L" ROWS FETCH NEXT "+std::to_wstring(limit)+L" ROWS ONLY";
    } else if(e!=Engine::Odbc) sql+=L" LIMIT "+std::to_wstring(limit)+L" OFFSET "+std::to_wstring(offset);
    return sql;
}
static bool Editable(const std::vector<Column>& columns, const Row& row, std::wstring& error) {
    if(columns.size()!=row.size() || columns.empty()) { error=L"Refresh the table before editing."; return false; }
    if(std::any_of(row.begin(),row.end(),[](const Value& v){return v.binary || v.truncated;})) {
        error=L"This row contains binary or truncated values. Use SQL with explicit parameters instead."; return false;
    }
    return true;
}
std::optional<Statement> UpdateRow(Engine e,const Table& table,const std::vector<Column>& cols,
                                  const Row& original,const Row& updated,bool remove,std::wstring& error) {
    if(!Editable(cols,original,error) || (!remove && !Editable(cols,updated,error))) return {};
    if(std::none_of(cols.begin(),cols.end(),[](const Column& c){return c.primaryKey;})) {
        error=L"A primary key is required for safe grid updates/deletes. Use the SQL editor for this object."; return {};
    }
    Statement statement; statement.expectOne=true;
    auto param=[&](size_t i,const Value& v){statement.parameters.push_back({v,cols[i].sqlType,cols[i].size});};
    statement.sql=remove?L"DELETE FROM ":L"UPDATE "; statement.sql+=QualifiedName(e,table);
    if(!remove) {
        bool any=false;
        for(size_t i=0;i<cols.size();++i) if(original[i]!=updated[i]) {
            if(cols[i].generated) {error=L"Generated columns cannot be changed.";return {};}
            statement.sql+=any?L", ":L" SET "; any=true;
            statement.sql+=QuoteIdentifier(e,cols[i].name)+L" = ?"; param(i,updated[i]);
        }
        if(!any){error=L"No values changed.";return {};}
    }
    // Compare the original row as well as its key: never overwrite a concurrent edit.
    for(size_t i=0;i<cols.size();++i) {
        statement.sql+=i?L" AND ":L" WHERE "; statement.sql+=QuoteIdentifier(e,cols[i].name);
        if(original[i].text) {statement.sql+=L" = ?";param(i,original[i]);} else statement.sql+=L" IS NULL";
    }
    return statement;
}
std::optional<Statement> InsertRow(Engine e,const Table& table,const std::vector<Column>& cols,
                                  const Row& row,const std::vector<bool>& include,std::wstring& error) {
    if(!Editable(cols,row,error) || include.size()!=cols.size()) return {};
    Statement out; out.expectOne=true; out.sql=L"INSERT INTO "+QualifiedName(e,table);
    std::wstring names,values;
    for(size_t i=0;i<cols.size();++i) if(include[i]) {
        if(cols[i].generated){error=L"Omit generated columns.";return {};}
        if(!names.empty()){names+=L", ";values+=L", ";}
        names+=QuoteIdentifier(e,cols[i].name); values+=L"?";
        out.parameters.push_back({row[i],cols[i].sqlType,cols[i].size});
    }
    if(names.empty()) out.sql+=e==Engine::MySQL?L" () VALUES ()":L" DEFAULT VALUES";
    else out.sql+=L" ("+names+L") VALUES ("+values+L")";
    return out;
}
std::wstring Csv(const ResultSet& result) {
    auto quote=[](std::wstring text){std::wstring out=L"\""; for(auto c:text){if(c==L'"')out+=L'"';out+=c;}return out+L"\"";};
    std::wstring out;
    for(size_t i=0;i<result.columns.size();++i){if(i)out+=L",";out+=quote(result.columns[i].name);} out+=L"\r\n";
    for(const auto& row:result.rows){for(size_t i=0;i<row.size();++i){if(i)out+=L",";if(row[i].text)out+=quote(*row[i].text);}out+=L"\r\n";}
    return out;
}
std::wstring JsonRows(const ResultSet& result) {
    // An explicit columns+rows representation preserves duplicate SQL aliases and NULL.
    auto root=json::Value::Object(); auto columns=json::Value::Array(),rows=json::Value::Array();
    for(const auto& col:result.columns)columns.push_back(WideToUtf8(col.name));
    for(const auto& row:result.rows){auto values=json::Value::Array();for(const auto& value:row)values.push_back(value.text?json::Value(WideToUtf8(*value.text)):json::Value());rows.push_back(values);}
    root["columns"]=columns;root["rows"]=rows;return Utf8ToWide(root.dump());
}
} // namespace ft::db
