# CLI Reference

The `widget` command manages widgets. Press <kbd>Ctrl</kbd>+<kbd>C</kbd> to abort.

Warning

Running `widget purge` deletes *all* data.

## Installation

```console
$ pip install widget
$ widget --version
```

## Usage

```bash
widget create --name "demo" --size 3
widget list | grep demo
```

Configuration lives in `~/.widgetrc`:

```json
{
  "color": "blue",
  "retries": 3
}
```

### Options

| Option          | Default | Description                              |
| --------------- | ------- | ---------------------------------------- |
| `--name`        | *none*  | Widget name (required).                  |
| `--size`        | `1`     | Size, from 1 to 10.<br>Larger is slower. |
| `--force`, `-f` | `false` | Overwrite existing widgets.              |

### Exit codes

**`0`**

Success.

**`2`**

Invalid usage. See [Usage](#usage).

## API

Call [`widget.create()`](api.html#widget.create) from Python:

```python
import widget
w = widget.create(name="demo")  # returns a Widget
print(w.size * 2)
```

See also: [Changelog](changelog.html), [GitHub](https://github.com/example/widget).
