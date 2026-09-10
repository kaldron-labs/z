# Curated TOML fixtures

This directory is a small repository-owned subset adapted from the grammar
categories exercised by `toml-test` for TOML 1.1. It intentionally keeps only
focused configuration-parser cases needed by `ZvTOMLTest`; routine tests do
not fetch or execute the upstream corpus.

Run the consolidated parser and file-I/O tests with `make -C zv/test test`.
