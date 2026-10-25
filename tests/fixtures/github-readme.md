# fastjson

[![CI](https://github.com/x/fastjson/workflows/CI/badge.svg)](https://github.com/x/fastjson/actions) [![Go Reference](https://camo.githubusercontent.com/abc/def)](https://pkg.go.dev/x/fastjson)

A **fast** JSON parser 🚀

## Install

```shell
go get -u github.com/x/fastjson
```

## Roadmap

- [x] Parser
- [ ] Streaming API

**Benchmarks**

```console
BenchmarkParse-8   5000000   300 ns/op
```

| Library       | ns/op |
| ------------- | ----: |
| fastjson      |   300 |
| encoding/json |  1500 |

Licensed under [MIT](https://github.com/x/fastjson/blob/main/LICENSE).
