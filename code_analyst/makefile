# 编译器与选项
CXX := clang++
CXXFLAGS := -std=c++17 -fms-compatibility -fmsc-version=1930 -DCLANG_STATIC_DEFINE -DLLVM_STATIC_LINK -Wno-unknown-attributes

# 修改为你 LLVM 构建输出目录
LLVM_BUILD_DIR := C:\Mydata\clang+llvm-20.1.8-x86_64-pc-windows-msvc\clang+llvm-20.1.8-x86_64-pc-windows-msvc

INCLUDE := \
	-isystem "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\include" \
	-isystem "C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\um" \
	-isystem "C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\shared" \
	-isystem "C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\ucrt" \
	-isystem $(LLVM_BUILD_DIR)/include \
	-isystem "C:\Users\16620\Desktop" \


	

# 如果你使用的是 MinGW 工具链，建议加上：
LDFLAGS :=  \
	-LC:/Users/16620/Desktop \
	-LC:\Mydata\clang+llvm-20.1.8-x86_64-pc-windows-msvc\clang+llvm-20.1.8-x86_64-pc-windows-msvc\lib \
	-L"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\lib\x64" \
	-L"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\um\x64" \
	-L"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\ucrt\x64" \
	

# 链接需要的 Clang 和 LLVM 库（最少）
# Clang 相关库
CLANG_LIBS :=  \
  -lclangAST \
   -llibclang \
  -lclangAnalysis \
  -lclangAnalysisFlowSensitive \
  -lclangAnalysisFlowSensitiveModels \
  -lclangAPINotes \
  -lclangApplyReplacements \
  -lclangASTMatchers \
  -lclangBasic \
  -lclangChangeNamespace \
  -lclangCodeGen \
  -lclangCrossTU \
  -lclangDaemon \
  -lclangDaemonTweaks \
  -lclangDependencyScanning \
  -lclangDirectoryWatcher \
  -lclangdMain \
  -lclangDoc \
  -lclangdRemoteIndex \
  -lclangDriver \
  -lclangdSupport \
  -lclangDynamicASTMatchers \
  -lclangEdit \
  -lclangExtractAPI \
  -lclangFormat \
  -lclangFrontend \
  -lclangFrontendTool \
  -lclangHandleCXX \
  -lclangHandleLLVM \
  -lclangIncludeCleaner \
  -lclangIncludeFixer \
  -lclangIncludeFixerPlugin \
  -lclangIndex \
  -lclangIndexSerialization \
  -lclangInstallAPI \
  -lclangInterpreter \
  -lclangLex \
  -lclangMove \
  -lclangParse \
  -lclangQuery \
  -lclangReorderFields \
  -lclangRewrite \
  -lclangRewriteFrontend \
  -lclangSema \
  -lclangSerialization \
  -lclangStaticAnalyzerCheckers \
  -lclangStaticAnalyzerCore \
  -lclangStaticAnalyzerFrontend \
  -lclangSupport \
  -lclangTidy \
  -lclangTidyAbseilModule \
  -lclangTidyAlteraModule \
  -lclangTidyAndroidModule \
  -lclangTidyBoostModule \
  -lclangTidyBugproneModule \
  -lclangTidyCERTModule \
  -lclangTidyConcurrencyModule \
  -lclangTidyCppCoreGuidelinesModule \
  -lclangTidyDarwinModule \
  -lclangTidyFuchsiaModule \
  -lclangTidyGoogleModule \
  -lclangTidyHICPPModule \
  -lclangTidyLinuxKernelModule \
  -lclangTidyLLVMLibcModule \
  -lclangTidyLLVMModule \
  -lclangTidyMain \
  -lclangTidyMiscModule \
  -lclangTidyModernizeModule \
  -lclangTidyMPIModule \
  -lclangTidyObjCModule \
  -lclangTidyOpenMPModule \
  -lclangTidyPerformanceModule \
  -lclangTidyPlugin \
  -lclangTidyPortabilityModule \
  -lclangTidyReadabilityModule \
  -lclangTidyUtils \
  -lclangTidyZirconModule \
  -lclangTooling \
  -lclangToolingASTDiff \
  -lclangToolingCore \
  -lclangToolingInclusions \
  -lclangToolingInclusionsStdlib \
  -lclangToolingRefactoring \
  -lclangToolingSyntax \
  -lclangTransformer

# LLVM 相关库（从 llvm-config --libs --system-libs all 得到）
LLVM_LIBS := \
  -lLLVMWindowsManifest -lLLVMXRay -lLLVMLibDriver -lLLVMDlltoolDriver \
  -lLLVMTelemetry -lLLVMTextAPIBinaryReader -lLLVMCoverage -lLLVMLineEditor \
  -lLLVMX86TargetMCA -lLLVMX86Disassembler -lLLVMX86AsmParser -lLLVMX86CodeGen \
  -lLLVMX86Desc -lLLVMX86Info -lLLVMOrcDebugging -lLLVMOrcJIT \
  -lLLVMWindowsDriver -lLLVMMCJIT -lLLVMJITLink -lLLVMInterpreter \
  -lLLVMExecutionEngine -lLLVMRuntimeDyld -lLLVMOrcTargetProcess \
  -lLLVMOrcShared -lLLVMDWP -lLLVMDebugInfoLogicalView \
  -lLLVMOption -lLLVMObjCopy -lLLVMMCA -lLLVMMCDisassembler -lLLVMLTO \
  -lLLVMPasses -lLLVMHipStdPar -lLLVMCFGuard -lLLVMCoroutines -lLLVMipo \
  -lLLVMVectorize -lLLVMSandboxIR -lLLVMLinker -lLLVMFrontendOpenMP \
  -lLLVMFrontendOffloading -lLLVMObjectYAML -lLLVMFrontendOpenACC \
  -lLLVMFrontendDriver -lLLVMInstrumentation  \
  -lLLVMFrontendAtomic -lLLVMExtensions -lLLVMDWARFLinkerParallel \
  -lLLVMDWARFLinkerClassic -lLLVMDWARFLinker -lLLVMGlobalISel -lLLVMMIRParser \
  -lLLVMAsmPrinter -lLLVMSelectionDAG -lLLVMCodeGen -lLLVMTarget \
  -lLLVMObjCARCOpts -lLLVMCodeGenTypes -lLLVMCGData -lLLVMIRPrinter \
  -lLLVMInterfaceStub -lLLVMFileCheck -lLLVMFuzzMutate -lLLVMScalarOpts \
  -lLLVMInstCombine -lLLVMAggressiveInstCombine -lLLVMTransformUtils \
  -lLLVMBitWriter -lLLVMAnalysis -lLLVMProfileData -lLLVMSymbolize \
  -lLLVMDebugInfoBTF -lLLVMDebugInfoPDB -lLLVMDebugInfoMSF \
  -lLLVMDebugInfoCodeView -lLLVMDebugInfoGSYM -lLLVMDebugInfoDWARF \
  -lLLVMObject -lLLVMTextAPI -lLLVMMCParser \
  -lLLVMIRReader -lLLVMAsmParser -lLLVMMC -lLLVMBitReader \
  -lLLVMFrontendHLSL -lLLVMFuzzerCLI -lLLVMCore -lLLVMRemarks \
  -lLLVMBitstreamReader -lLLVMBinaryFormat -lLLVMTargetParser \
  -lLLVMTableGen -lLLVMSupport -lLLVMDemangle

SYSTEM_LIBS := -lpsapi -lshell32 -lole32 -luuid -ladvapi32 -lws2_32 -lntdll -lversion
THIRD_PARTY_LIBS := \
  -ljsonloader

LDLIBS := $(CLANG_LIBS) $(LLVM_LIBS) $(SYSTEM_LIBS) $(THIRD_PARTY_LIBS)

# 源文件与目标文件
SRC := ast_test.cpp
OBJ := $(SRC:.cpp=.o)
TARGET := only_derived.exe

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(INCLUDE) $(LDFLAGS) -o $@ $^ $(LDLIBS) 
# $(TARGET): $(SRC)
# 	$(CXX) $(CXXFLAGS) $(INCLUDE) $(LDFLAGS) ast_test.cpp -H -c

clean:
	del /Q *.exe *.o *.obj 2>nul || true