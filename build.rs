fn main() {
    let sandstorm_path = std::env::var("SANDSTORM_PATH")
        .expect(
            "SANDSTORM_PATH environment variable not defined; please \
            set it to the path to the sandstorm source code."
        );
    let sandstorm_schema_path = sandstorm_path + "/src/sandstorm";
    capnpc::CompilerCommand::new()
        .src_prefix(&sandstorm_schema_path)
        .import_path(&sandstorm_schema_path)
        .file("filesystem.capnp")
        .file(sandstorm_schema_path + "/util.capnp")
        .run()
        .expect("schema compiler command")
}
