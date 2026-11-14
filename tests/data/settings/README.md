# Fixture for the settings import

`MusicBee3Settings.ini` is written by hand in the shape MusicBee 3 gives
its settings file: UTF-8, CRLF line ends, a root element
`ApplicationSettings`, simple settings as child elements, a `HotKeys`
list, and naming rules. Like the real thing it holds the character
U+0001 inside one value (`ArtistSortMapping`), which makes it not
well-formed XML. Every name, path and value in it is invented; it is not
a copy of anyone's file.
