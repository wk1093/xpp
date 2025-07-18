#include "file_writer.h"
#include "visitor.h"
#include <filesystem>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <regex>

std::vector<std::string> preprocStored;

std::string prePreprocess(const std::string& inputFile, const std::string& outputDir) {
    std::ifstream in(inputFile);
    if (!in) {
        std::cerr << "Failed to open input file for pre-preprocessing: " << inputFile << "\n";
        exit(1);
    }
    std::ostringstream out;
    std::string line;

    std::regex pubPreproc(R"(^\s*#pub\s+(.*?)$)");
    std::regex importPreproc(R"(^\s*#import\s+(.*?)$)"); // #import X should act like #pub include X
    while (std::getline(in, line)) {
        std::smatch m;
        if (std::regex_search(line, m, pubPreproc)) {
            preprocStored.push_back(m[1].str());
            out << "pub __attribute__((annotate(\"__pub_preproc__\"))) void __pub_preproc__" << preprocStored.size() - 1 << "();\n";
        } else if (std::regex_search(line, m, importPreproc)) {
            std::string importedFile = m[1].str();
            // if it is "something.yapp" or <something.yapp> we need to convert it to "something.yapp.h" or <something.yapp.h>
            
            bool angledQuotes = (importedFile[0] == '<' && importedFile[importedFile.size() - 1] == '>');
            bool doubleQuotes = (importedFile[0] == '"' && importedFile[importedFile.size() - 1] == '"');
            if (angledQuotes || doubleQuotes) {
                // Remove the angled or double quotes
                importedFile = importedFile.substr(1, importedFile.size() - 2);
            }
            if (importedFile.find(".yapp") != std::string::npos) {
                importedFile = importedFile.substr(0, importedFile.find_last_of('.')) + ".yapp.h";
            }
            if (angledQuotes) {
                importedFile = "<" + importedFile + ">";
            } else if (doubleQuotes) {
                importedFile = "\"" + importedFile + "\"";
            } else {
                importedFile = "\"" + importedFile + ".yapp.h\"";
            }
            
            preprocStored.push_back("include " + importedFile);
            out << "pub __attribute__((annotate(\"__pub_preproc__\"))) void __pub_preproc__" << preprocStored.size() - 1 << "();\n";
        } else {
            out << line << "\n";
        }
    }
    // std::string outFile = inputFile + ".pubpp";
    std::string outFile = std::filesystem::path(outputDir) / (inputFile.substr(inputFile.find_last_of("/\\") + 1) + ".pubpp");
    std::ofstream pubpp(outFile);
    if (!pubpp) {
        std::cerr << "Failed to write pre-preprocessed file: " << outFile << "\n";
        exit(1);
    }
    pubpp << out.str();
    pubpp.close();
    return outFile;
}

const char* usageString = R"(
Usage: yappc <source.yapp> [OPTIONS] -- [Preprocessor Args] -- [Compiler Args]
Options:
    -s          Toggle whether the source file is left in the output. This is useful for debugging.
    -g          Show generated intermediate preprocessing files.
    -h          Show this help message
    -o <output> Specify output folder for generated files (default: same as input file)
    -r          Remove the header from the generated .yapp.cpp file (not very useful)
    -c          Compile the generated files.
    --          Separator for different argument sections
Preprocessor Args:
    These arguments are passed directly to the preprocessor (clang -E).
    You can use -D to define macros, e.g., -DDEBUG=1.
Compiler Args:
    These arguments are passed directly to the compiler (clang++).
    You can use -o to specify the output file.
)";

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: ./yappc <source.yapp> [OPTIONS] -- [Preprocessor Args] -- [Compiler Args]\n";
        return 1;
    }

    std::vector<std::string> preprocArgs;
    std::vector<std::string> compileArgs;
    std::vector<std::string> myArgs;
    // bool isPreprocArgs = true;
    // for (int i = 2; i < argc; ++i) {
    //     if (strcmp(argv[i], "--") == 0) {
    //         isPreprocArgs = false;
    //         continue;
    //     }
    //     if (isPreprocArgs) {
    //         preprocArgs.push_back(argv[i]);
    //     } else {
    //         compileArgs.push_back(argv[i]);
    //     }
    // }

    int argState = 0; // 0: myargs, 1: preprocArgs, 2: compileArgs
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--") == 0) {
            argState++;
            continue;
        }
        if (argState == 0) {
            myArgs.push_back(argv[i]);
        } else if (argState == 1) {
            preprocArgs.push_back(argv[i]);
        } else if (argState == 2) {
            compileArgs.push_back(argv[i]);
        }
    }
    // Check for -d option in myArgs
    bool debugMode = false;
    bool sourceToggle = false;
    bool removeHeader = false;
    bool compileGenerated = false;
    std::string outputDir = ""; // Default to same directory as input file
    for (int i = 0; i < myArgs.size(); i++) {
        std::string arg = myArgs[i];
        if (arg == "-s") {
            sourceToggle = true;
        }
        if (arg == "-g") {
            debugMode = true;
        }
        if (arg == "-h") {
            std::cout << usageString;
            return 0;
        }
        if (arg == "-o") {
            // Handle output directory option
            if (i + 1 >= myArgs.size()) {
                // If -o is the last argument, we can't set an output directory
                std::cerr << "Error: -o option requires an argument.\n";
                return 1; 
            } else if (myArgs[i + 1][0] == '-') {
                // If the next argument is another option, we can't set an output directory
                std::cerr << "Error: -o option requires a directory argument.\n";
                return 1;
            } else {
                outputDir = myArgs[i + 1];
                i++; // Skip the next argument since it's the directory
                std::filesystem::path dirPath(outputDir);
                if (!std::filesystem::exists(dirPath)) {
                    std::cerr << "Warning: Specified output directory does not exist. Creating: " << outputDir << "\n";
                    std::filesystem::create_directories(dirPath);
                }
            }
        }
        if (arg == "-r") {
            // Handle remove header option
            removeHeader = true;
        }
        if (arg == "-c") {
            // Handle compile generated files option
            compileGenerated = true;
        }
    }

    std::string base = argv[1];
    size_t slash = base.find_last_of("/\\");
    size_t dot = base.find_last_of('.');
    std::string filename = base.substr(slash == std::string::npos ? 0 : slash + 1, dot - (slash == std::string::npos ? 0 : slash + 1));
    std::string dir = (slash == std::string::npos) ? "./" : base.substr(0, slash + 1);
    if (outputDir.empty()) {
        outputDir = dir; // Default to same directory as input file
    }

    // pre-preprocessor step
    std::string pubppFile = prePreprocess(argv[1], outputDir);
    // preprocessor step
    std::string preprocOutputFile = pubppFile;
    if (preprocOutputFile.find(".yapp.pubpp") == std::string::npos) {
        std::cerr << "Input file must have .yapp extension.\n";
        return 1;
    }
    preprocOutputFile = preprocOutputFile.substr(0, preprocOutputFile.size() - 6) + ".pyapp";
    std::string preprocCommand = "clang -E ";
    for (const auto& arg : preprocArgs) {
        preprocCommand += "\"" + arg + "\" ";
    }
    preprocCommand += "\"-Dpub=__attribute__((annotate(\\\"pub\\\")))\" ";
    preprocCommand += "\"-Dpriv=__attribute__((annotate(\\\"priv\\\")))\" ";
    preprocCommand += "-D__yaplusplus ";
    preprocCommand += "-x c++ \"" + pubppFile + "\" > \"" + preprocOutputFile + "\"";
    if (system(preprocCommand.c_str()) != 0) {
        std::cerr << "Preprocessing failed.\n";
        return 1;
    }

    std::ifstream preprocFile(preprocOutputFile);
    if (!preprocFile) {
        std::cerr << "Failed to open preprocessed file: " << preprocOutputFile << "\n";
        return 1;
    }
    std::string line;
    std::ostringstream cleanedOutput;
    while (std::getline(preprocFile, line)) {
        if (line.empty() || line[0] != '#') {
            cleanedOutput << line << "\n";
        } else {
            cleanedOutput << "\n";
        }
    }
    preprocFile.close();
    std::ofstream cleanedFile(preprocOutputFile);
    if (!cleanedFile) {
        std::cerr << "Failed to write cleaned preprocessed file: " << preprocOutputFile << "\n";
        return 1;
    }
    cleanedFile << cleanedOutput.str();
    cleanedFile.close();

    CXIndex index = clang_createIndex(0, 0);
    const char* args[] = {
        "-x", "c++-cpp-output",
        "-std=c++20",
        "-Dpub=__attribute__((annotate(\"pub\")))",
        "-Dpriv=__attribute__((annotate(\"priv\")))"
    };
    CXTranslationUnit tu = clang_parseTranslationUnit(
        index, preprocOutputFile.c_str(), args, sizeof(args)/sizeof(args[0]), nullptr, 0, CXTranslationUnit_None);
    if (!tu) {
        if (unsigned numErrors = clang_getNumDiagnostics(tu)) {
            unsigned displayOptions = clang_defaultDiagnosticDisplayOptions();
            for (unsigned i=0; i < numErrors; ++i) {
                CXDiagnostic diag = clang_getDiagnostic(tu, i);
                CXString str = clang_formatDiagnostic(diag, displayOptions);
                std::cerr << clang_getCString(str) << "\n";
                clang_disposeString(str);
                clang_disposeDiagnostic(diag);
            }
            return 2;
        }
        std::cerr << "Failed to parse input file.\n";
        return 1;
    }

    CXCursor root = clang_getTranslationUnitCursor(tu);
    clang_visitChildren(root, visitor, &tu);

    writeFiles(filename, outputDir);
    clang_disposeTranslationUnit(tu);
    clang_disposeIndex(index);
    if (compileGenerated) {
        std::string outdirfilename = std::filesystem::path(outputDir) / (filename + ".yapp.cpp");
        std::string compileCommand = "clang++ \"" + outdirfilename + "\" ";
        for (const auto& arg : compileArgs) {
            compileCommand += "\"" + arg + "\" ";
        }
        if (system(compileCommand.c_str()) != 0) {
            std::cerr << "Compilation failed.\n";
            return 1;
        }
    }

    if (!debugMode) {
        // remove intermediate files
        std::remove(preprocOutputFile.c_str());
        std::remove(pubppFile.c_str());

        if ((!compileGenerated && sourceToggle) || (compileGenerated && !sourceToggle)) {
            std::remove((std::filesystem::path(outputDir) / (filename + ".yapp.cpp")).c_str());
        }

        if (removeHeader) {
            std::remove((std::filesystem::path(outputDir) / (filename + ".yapp.h")).c_str());
        }
    }

    return 0;
}