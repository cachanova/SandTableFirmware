"""Expose validated multipart completion in pinned ESPAsyncWebServer 3.9.5.

The upstream file-final callback precedes validation of the closing boundary.
A final-request handler alone cannot distinguish a truncated MIME terminator.
Apply this narrow, idempotent patch after PlatformIO resolves dependencies.
"""
from pathlib import Path


def patch(directory):
    header = directory / 'ESPAsyncWebServer.h'
    source = directory / 'WebRequest.cpp'
    h = header.read_text()
    s = source.read_text()
    if 'bool multipartComplete() const;' in h:
        if 'Sisyphus multipart terminator validation' not in s:
            raise RuntimeError('Partial ESPAsyncWebServer upload patch')
        return
    anchor = '  bool multipart() const {'
    if h.count(anchor) != 1:
        raise RuntimeError('Unexpected ESPAsyncWebServer header; review upload patch')
    h = h.replace(anchor, '  bool multipartComplete() const;\n' + anchor)
    anchor = 'void AsyncWebServerRequest::_parseMultipartPostByte(uint8_t data, bool last) {'
    if s.count(anchor) != 1 or s.count('_multiParseState = PARSING_FINISHED;') != 1:
        raise RuntimeError('Unexpected ESPAsyncWebServer parser; review upload patch')
    s = s.replace(anchor, '''// Sisyphus multipart terminator validation: require all of --boundary--\\r\\n.
bool AsyncWebServerRequest::multipartComplete() const {
  return _isMultipart && _multiParseState == PARSING_FINISHED &&
         _boundaryPosition == 3 && _parsedLength == _contentLength;
}

''' + anchor + '''
  if (_multiParseState == PARSING_FINISHED) {
    const char ending[] = "-\\r\\n";
    if (_boundaryPosition >= 3 || data != static_cast<uint8_t>(ending[_boundaryPosition]))
      _multiParseState = PARSE_ERROR;
    else
      ++_boundaryPosition;
    return;
  }
''')
    s = s.replace('_multiParseState = PARSING_FINISHED;',
                  '_multiParseState = PARSING_FINISHED;\n      _boundaryPosition = 0;')
    header.write_text(h)
    source.write_text(s)


if __name__ == '__main__':
    import sys
    patch(Path(sys.argv[1]))
else:
    Import('env')  # noqa: F821 - PlatformIO/SCons
    patch(Path(env.subst('$PROJECT_LIBDEPS_DIR')) / env.subst('$PIOENV') / 'ESPAsyncWebServer' / 'src')
