#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/RecordLayout.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <fstream>
#include <set>
#include <vector>
#include <json/json.h>

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/Lex/Lexer.h>
#include <clang/AST/Attr.h>

using namespace clang;
using namespace clang::tooling;
using namespace clang::ast_matchers;

// ---- 全局收集结果 ----
static bool BaseFound = false;
struct Collected {
    std::string qualifiedName;
    Json::Value json; // 预先算好布局，最后按数量判断是否写文件
};
static std::set<const CXXRecordDecl*> Seen; // 用 canonical decl 去重
static std::vector<Collected> DerivedResults;
static void collectDecorators(const clang::Decl* D, clang::ASTContext& Ctx, Json::Value& out);

// 小工具
static inline std::string trim(std::string s) {
  auto l = s.find_first_not_of(" \t\r\n");
  auto r = s.find_last_not_of(" \t\r\n");
  if (l == std::string::npos) return {};
  return s.substr(l, r - l + 1);
}

// 解析一行形如： "@meta clamp(0.0,1.0) invisible"
static void parseMetaLine(const std::string& line, Json::Value& out /* array */) {
  // 找到 @meta（容忍前缀空白和注释装饰）
  auto p = line.find("@meta");
  if (p == std::string::npos) return;

  // 扫描 @meta 之后的 token：name 或 name(args)
  size_t i = p + 5; // 跳过 "@meta"
  auto isIdChar = [](char c){ return std::isalnum((unsigned char)c) || c=='_'; };

  while (i < line.size()) {
    // 跳空白
    while (i < line.size() && std::isspace((unsigned char)line[i])) ++i;
    if (i >= line.size()) break;

    // 读 name
    size_t nameL = i;
    while (i < line.size() && isIdChar(line[i])) ++i;
    if (i == nameL) { ++i; continue; } // 无法识别，跳过一个字符
    std::string name = line.substr(nameL, i - nameL);

    Json::Value one;
    one["name"] = name;

    // 可选参数
    if (i < line.size() && line[i] == '(') {
      int depth = 1; size_t a = ++i; // a 指向第一个参数字符
      for (; i < line.size() && depth; ++i) {
        if (line[i] == '(') ++depth;
        else if (line[i] == ')') --depth;
      }
      std::string args_str = (depth==0 ? line.substr(a, (i-1) - a)
                                       : line.substr(a)); // 容错：缺右括号
      // 按逗号拆分并 trim
      Json::Value args(Json::arrayValue);
      size_t s = 0;
      while (s <= args_str.size()) {
        size_t c = args_str.find(',', s);
        std::string item = trim(args_str.substr(s, (c==std::string::npos?args_str.size():c) - s));
        if (!item.empty()) args.append(item);
        if (c == std::string::npos) break;
        s = c + 1;
      }
      if (!args.empty()) one["args"] = std::move(args);
    }

    out.append(std::move(one));
  }
}

static Json::Value buildLayoutJson(ASTContext& Ctx, const CXXRecordDecl* RD) {
    Json::Value j;
    j["class"] = RD->getQualifiedNameAsString();

    const ASTRecordLayout& layout = Ctx.getASTRecordLayout(RD);
    j["size_bytes"] = Json::UInt64(layout.getSize().getQuantity());
    j["alignment"]  = Json::UInt64(layout.getAlignment().getQuantity());

    Json::Value fields(Json::arrayValue);

    // 递归基类字段
    auto collectFields = [&](const CXXRecordDecl* R, const ASTRecordLayout& L,
                             Json::Value& out, unsigned baseOffsetBits,
                             auto&& collectRef) -> void {
        for (const auto& base : R->bases()) {
            const auto* BD = base.getType()->getAsCXXRecordDecl();
            if (BD && BD->hasDefinition()) {
                const ASTRecordLayout& BL = Ctx.getASTRecordLayout(BD);
                uint64_t off = L.getBaseClassOffset(BD).getQuantity() * 8;
                collectRef(BD, BL, out, baseOffsetBits + off, collectRef);
            }
        }
        unsigned i = 0;
        for (const FieldDecl* F : R->fields()) {
            Json::Value fj;
            fj["name"] = F->getNameAsString();
            fj["type"] = F->getType().getAsString();
            fj["offset_bits"] = Json::UInt64(baseOffsetBits + L.getFieldOffset(i));

            if (const Expr* init = F->getInClassInitializer()) {
                std::string str;
                llvm::raw_string_ostream s(str);
                init->printPretty(s, nullptr, PrintingPolicy(LangOptions()));
                fj["default"] = s.str();
            } else {
                fj["default"] = Json::Value();
            }

            // ← 新增：收集 decorators（可能 0~N 条）
            Json::Value decorators(Json::arrayValue);
            ::collectDecorators(F, Ctx, decorators);
            if (!decorators.empty())
                fj["decorators"] = std::move(decorators);

            out.append(fj);
            ++i;
        }
    };
    collectFields(RD, layout, fields, 0, collectFields);
    j["fields"] = fields;
    return j;
}



// === 用 Attr 基类 + 源码切片解析 annotate，不用 AnnotateAttr 成员函数 ===
static void collectDecorators(const clang::Decl* D, clang::ASTContext& Ctx, Json::Value& out) {
            //     for (const Attr *A : D->attrs()) {
            //         llvm::dyn_cast<clang::AnnotateAttr>(A);
            //     if (const auto *Ann = dyn_cast<AnnotateAttr>(A)) {
            //     // 字符串内容：
            //     StringRef s = Ann->getAnnotation();
            //     // 位置/源区间等：
            //     SourceRange R = Ann->getRange();
            //     // TODO: 你的处理逻辑
            //     }
            // }
    auto* RC = Ctx.getRawCommentForDeclNoCache(D);
    if (!RC) return;

    // 拿注释原文（保留换行，便于逐行处理）
    std::string raw = RC->getRawText(Ctx.getSourceManager()).str();

    // 逐行找 "@meta ..."
    size_t start = 0;
    while (start < raw.size()) {
        size_t end = raw.find('\n', start);
        std::string line = (end==std::string::npos) ? raw.substr(start)
                                                    : raw.substr(start, end-start);
        // 去掉常见注释前缀
        // 例如 "/// xxx", "// xxx", "/* xxx */", " * xxx"
        auto ltrim = trim(line);
        if (!ltrim.empty()) {
        // 去掉开头的注释修饰
        if (ltrim.rfind("///", 0) == 0) ltrim = trim(ltrim.substr(3));
        else if (ltrim.rfind("//", 0) == 0) ltrim = trim(ltrim.substr(2));
        else if (ltrim.rfind("*", 0) == 0) ltrim = trim(ltrim.substr(1));
        parseMetaLine(ltrim, out);
        }

        if (end == std::string::npos) break;
        start = end + 1;
  }
}

class BaseMarkerCB : public MatchFinder::MatchCallback {
public:
    void run(const MatchFinder::MatchResult& R) override {
        if (const auto* B = R.Nodes.getNodeAs<CXXRecordDecl>("baseDef")) {
            if (B->isThisDeclarationADefinition())
                BaseFound = true;
        }
    }
};



class DerivedCollectorCB : public MatchFinder::MatchCallback {
public:
    explicit DerivedCollectorCB(const std::string& baseName)
        : baseName(baseName) {}

    void run(const MatchFinder::MatchResult& R) override {
        const auto* RD = R.Nodes.getNodeAs<CXXRecordDecl>("derived");
        if (!RD || !RD->isThisDeclarationADefinition()) return;

        // 忽略基类本身
        if (RD->getQualifiedNameAsString() == baseName) return;

        // 去重：只收集 canonical 定义
        const CXXRecordDecl* Canon = RD->getCanonicalDecl();
        if (!Seen.insert(Canon).second) return;

        Json::Value j = buildLayoutJson(*R.Context, RD);
        Collected c{ RD->getQualifiedNameAsString(), std::move(j) };
        DerivedResults.emplace_back(std::move(c));
    }

private:
    std::string baseName;
};

class LayoutFrontendAction : public ASTFrontendAction {
public:
    LayoutFrontendAction(const std::string& baseName) 
        : baseName(baseName), derivedCB(baseName) {}

    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance&, llvm::StringRef) override {
        finder.addMatcher(
            cxxRecordDecl(hasName(baseName), isDefinition()).bind("baseDef"),
            &baseCB);

        finder.addMatcher(
            cxxRecordDecl(isDefinition(), isDerivedFrom(hasName(baseName)))
                .bind("derived"),
            &derivedCB);

        return finder.newASTConsumer();
    }

private:
    std::string baseName;
    BaseMarkerCB baseCB;
    DerivedCollectorCB derivedCB;
    MatchFinder finder;
};


class Factory : public FrontendActionFactory {
public:
    explicit Factory(const std::string& baseName) : baseName(baseName) {}
    std::unique_ptr<FrontendAction> create() override {
        return std::make_unique<LayoutFrontendAction>(baseName);
    }
private:
    std::string baseName;
};

static llvm::cl::OptionCategory ToolCategory("ast-export-tool");
static llvm::cl::opt<std::string> ClassName("class",
    llvm::cl::desc("Base class name to search derived from"),
    llvm::cl::value_desc("BaseClass"), llvm::cl::Required, llvm::cl::cat(ToolCategory));
static llvm::cl::opt<std::string> OutputFilename("o",
    llvm::cl::desc("Output JSON file"),
    llvm::cl::value_desc("filename"), llvm::cl::Required, llvm::cl::cat(ToolCategory));

int main(int argc, const char** argv) {
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, ToolCategory);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1; // 解析失败
    }

    CommonOptionsParser& OptionsParser = ExpectedParser.get();
    ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());

    // 跑一遍：标记基类是否定义、收集所有派生类
    int ret = Tool.run(new Factory(ClassName));
    if (ret != 0) return ret; // Clang 本身出错

    if (!BaseFound) {
        llvm::errs() << "Error: base class '" << ClassName << "' has no definition in TU.\n";
        return 2;
    }

    if (DerivedResults.empty()) {
        llvm::errs() << "Error: no subclasses derived from '" << ClassName << "'.\n";
        return 4;
    }

    if (DerivedResults.size() > 1) {
        llvm::errs() << "Error: multiple subclasses derived from '" << ClassName << "':\n";
        for (auto& c : DerivedResults) llvm::errs() << "  - " << c.qualifiedName << "\n";
        return 3;
    }

    // 恰好一个——写 JSON
    {
        std::ofstream out(OutputFilename);
        if (!out) {
            llvm::errs() << "Error: cannot open output file: " << OutputFilename << "\n";
            return 5;
        }
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "    ";
        std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
        writer->write(DerivedResults[0].json, &out);
    }

    return 0;
}
