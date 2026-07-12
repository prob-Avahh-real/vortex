#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

#include "parser.h"
#include "cpp_generator.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: vortex-codegen [--lang=cpp] [--out=<dir>] <input.proto>\n";
        return 1;
    }

    std::string input_file;
    std::string output_dir = ".";
    std::string lang = "cpp";

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.starts_with("--lang=")) {
            lang = arg.substr(7);
        } else if (arg.starts_with("--out=")) {
            output_dir = arg.substr(6);
        } else {
            input_file = arg;
        }
    }

    if (input_file.empty()) {
        std::cerr << "Error: no input file specified\n";
        return 1;
    }

    // Read input
    std::ifstream ifs(input_file);
    if (!ifs) {
        std::cerr << "Error: cannot open " << input_file << "\n";
        return 1;
    }

    std::stringstream buffer;
    buffer << ifs.rdbuf();
    std::string source = buffer.str();

    // Parse
    vortex::idl::Parser parser(source);
    auto result = parser.parse_file();

    if (!result.has_value()) {
        std::cerr << "Parse errors:\n";
        for (auto& err : parser.errors()) {
            std::cerr << "  " << err << "\n";
        }
        return 1;
    }

    auto& file = *result;

    // Generate C++
    if (lang == "cpp") {
        vortex::idl::CppGenerator::Options opts;
        opts.namespace_name = file.package;

        vortex::idl::CppGenerator gen(opts);
        auto generated = gen.generate(file);

        // Determine output filename
        // Derive from input file name: foo.proto → foo.vortex.h
        std::string base = file.file_name;
        if (base == "<input>") {
            // Derive from actual input filename
            base = input_file;
            auto slash = base.find_last_of('/');
            if (slash != std::string::npos) base = base.substr(slash + 1);
        }
        auto dot = base.rfind(".proto");
        if (dot != std::string::npos) base = base.substr(0, dot);
        std::string output_file = output_dir + "/" + base + ".vortex.h";

        std::ofstream ofs(output_file);
        if (!ofs) {
            std::cerr << "Error: cannot write " << output_file << "\n";
            return 1;
        }

        ofs << generated;
        std::cout << "Generated: " << output_file << " ("
                  << generated.size() << " bytes)\n";
    }

    return 0;
}
