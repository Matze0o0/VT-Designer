// vtpool-cli: inspect, validate, import and build VT object pools without the GUI.

#include "vtcore/Exporters.h"
#include "vtcore/IopCodec.h"
#include "vtcore/ProjectFile.h"
#include "vtcore/Schema.h"
#include "vtcore/Validator.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr int kExitOk = 0;
constexpr int kExitValidationFailed = 1;
constexpr int kExitUsage = 2;
constexpr int kExitFailure = 3;

void printUsage()
{
    std::cout << "Usage:\n"
                 "  vtpool-cli info <file>                       List the objects of a project or .iop file\n"
                 "  vtpool-cli validate <file>                   Check a pool against ISO 11783-6\n"
                 "  vtpool-cli import <pool.iop> <project.vtp>   Convert a binary pool into a project\n"
                 "  vtpool-cli build <project.vtp> [out-dir]     Write .iop, ID header and C array\n"
                 "\n"
                 "Options:\n"
                 "  --vt-version <3|4|5>   VT version of .iop input (default 4)\n";
}

struct Arguments {
    std::string command;
    std::vector<std::string> files;
    vt::VtVersion version = vt::VtVersion::V4;
};

class UsageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

Arguments parseArguments(int argc, char** argv)
{
    Arguments args;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--vt-version") {
            if (i + 1 >= argc) {
                throw UsageError("--vt-version needs a value");
            }
            const auto version = vt::vtVersionFromInt(std::atoi(argv[++i]));
            if (!version) {
                throw UsageError("Supported VT versions are 3, 4 and 5");
            }
            args.version = *version;
        } else if (arg.rfind("--", 0) == 0) {
            throw UsageError("Unknown option " + arg);
        } else if (args.command.empty()) {
            args.command = arg;
        } else {
            args.files.push_back(arg);
        }
    }
    return args;
}

bool isIopFile(const fs::path& path)
{
    std::string extension = path.extension().string();
    for (char& c : extension) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return extension == ".iop";
}

vt::Project loadAny(const fs::path& path, vt::VtVersion version)
{
    if (isIopFile(path)) {
        vt::Project project{vt::decodePool(vt::readBinaryFile(path), version), {}};
        project.exportOptions.poolName = path.stem().string();
        return project;
    }
    return vt::loadProject(path);
}

void printDiagnostics(const std::vector<vt::Diagnostic>& diagnostics)
{
    for (const vt::Diagnostic& d : diagnostics) {
        std::cout << (d.severity == vt::Severity::Error ? "error:   " : "warning: ") << d.message << "\n";
    }
}

int countErrors(const std::vector<vt::Diagnostic>& diagnostics)
{
    int errors = 0;
    for (const vt::Diagnostic& d : diagnostics) {
        errors += d.severity == vt::Severity::Error ? 1 : 0;
    }
    return errors;
}

int commandInfo(const Arguments& args)
{
    const vt::Project project = loadAny(args.files.at(0), args.version);
    std::cout << "VT version " << vt::toInt(project.pool.version()) << ", " << project.pool.size() << " objects\n";
    for (const vt::VtObject& object : project.pool.objects()) {
        std::cout << "  " << object.id << "\t" << object.schema().label;
        if (!object.name.empty()) {
            std::cout << "\t" << object.name;
        }
        std::cout << "\n";
    }
    return kExitOk;
}

int commandValidate(const Arguments& args)
{
    const vt::Project project = loadAny(args.files.at(0), args.version);
    const std::vector<vt::Diagnostic> diagnostics = vt::validatePool(project.pool);
    printDiagnostics(diagnostics);
    const int errors = countErrors(diagnostics);
    std::cout << errors << " error(s), " << diagnostics.size() - static_cast<std::size_t>(errors) << " warning(s)\n";
    return errors > 0 ? kExitValidationFailed : kExitOk;
}

int commandImport(const Arguments& args)
{
    if (args.files.size() < 2) {
        throw UsageError("import needs an input .iop and an output project file");
    }
    const fs::path input = args.files[0];
    const fs::path output = args.files[1];
    vt::Project project{vt::decodePool(vt::readBinaryFile(input), args.version), {}};
    project.exportOptions.poolName = input.stem().string();
    vt::saveProject(project, output);
    std::cout << "Imported " << project.pool.size() << " objects into " << output.string() << "\n";
    return kExitOk;
}

int commandBuild(const Arguments& args)
{
    const fs::path input = args.files.at(0);
    const fs::path outDir = args.files.size() > 1 ? fs::path(args.files[1]) : input.parent_path();
    const vt::Project project = loadAny(input, args.version);

    const std::vector<vt::Diagnostic> diagnostics = vt::validatePool(project.pool);
    printDiagnostics(diagnostics);
    if (vt::hasErrors(diagnostics)) {
        std::cout << "Build aborted: the pool has " << countErrors(diagnostics) << " error(s)\n";
        return kExitValidationFailed;
    }

    if (!outDir.empty()) {
        fs::create_directories(outDir);
    }
    const vt::ExportOptions& options = project.exportOptions;
    const std::vector<std::uint8_t> iop = vt::encodePool(project.pool);

    const fs::path iopPath = outDir / vt::iopFileName(options);
    const fs::path idsPath = outDir / vt::idHeaderFileName(options);
    const fs::path arrayHeaderPath = outDir / vt::arrayHeaderFileName(options);
    const fs::path arraySourcePath = outDir / vt::arraySourceFileName(options);

    vt::writeBinaryFile(iopPath, iop);
    vt::writeTextFile(idsPath, vt::exportIdHeader(project.pool, options));
    vt::writeTextFile(arrayHeaderPath, vt::exportPoolArrayHeader(options));
    vt::writeTextFile(arraySourcePath, vt::exportPoolArraySource(iop, options));

    std::cout << "Wrote " << iopPath.string() << " (" << iop.size() << " bytes)\n"
              << "Wrote " << idsPath.string() << "\n"
              << "Wrote " << arrayHeaderPath.string() << "\n"
              << "Wrote " << arraySourcePath.string() << "\n";
    return kExitOk;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const Arguments args = parseArguments(argc, argv);
        if (args.command.empty() || args.command == "help" || args.files.empty()) {
            printUsage();
            return args.command == "help" ? kExitOk : kExitUsage;
        }
        if (args.command == "info") {
            return commandInfo(args);
        }
        if (args.command == "validate") {
            return commandValidate(args);
        }
        if (args.command == "import") {
            return commandImport(args);
        }
        if (args.command == "build") {
            return commandBuild(args);
        }
        throw UsageError("Unknown command '" + args.command + "'");
    } catch (const UsageError& e) {
        std::cerr << "vtpool-cli: " << e.what() << "\n\n";
        printUsage();
        return kExitUsage;
    } catch (const std::exception& e) {
        std::cerr << "vtpool-cli: " << e.what() << "\n";
        return kExitFailure;
    }
}
