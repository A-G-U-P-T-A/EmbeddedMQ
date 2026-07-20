module github.com/A-G-U-P-T-A/EmbeddedMQ/examples/loadtest/go

go 1.22

require github.com/A-G-U-P-T-A/EmbeddedMQ/bindings/go v1.0.0-beta.2

// Nested module needs sibling bindings/native; use monorepo path for local/CI load tests.
// Remote go get alone cannot see ../native until that tree is vendored into the Go module.
replace github.com/A-G-U-P-T-A/EmbeddedMQ/bindings/go => ../../../bindings/go
