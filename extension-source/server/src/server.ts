import {
  createConnection,
  TextDocuments,
  ProposedFeatures,
  CompletionItem,
  CompletionItemKind,
  TextDocumentPositionParams,
  Hover,
  Diagnostic,
  TextDocumentSyncKind,
  InitializeParams,
  InitializeResult,
  CompletionParams,
  SignatureHelp,
  SignatureHelpParams,
  SignatureInformation,
  ParameterInformation,
  DiagnosticSeverity,
  CodeAction,
  CodeActionKind,
  TextEdit,
  Range,
  InsertTextFormat,
  Definition,
  DocumentSymbol,
  SymbolKind,
  Location,
  RenameParams,
  WorkspaceEdit
} from 'vscode-languageserver/node';
import { TextDocument } from 'vscode-languageserver-textdocument';
import * as fs from 'fs';
import * as path from 'path';

const connection = createConnection(ProposedFeatures.all);
const documents: TextDocuments<TextDocument> = new TextDocuments(TextDocument);

// ─── Type System ─────────────────────────────────────────────────────────────

export enum TypeKind {
  Number = 'number',
  BigInt = 'bigint',
  Date = 'date',
  String = 'string',
  Boolean = 'boolean',
  Array = 'array',
  Object = 'object',
  Map = 'map',
  Null = 'null',
  Undefined = 'undefined',
  Regex = 'regex',
  Void = 'void',
  Error = 'Error',
  Function = 'function',
  Unknown = 'unknown'
}

export interface TypeInfo {
  kind: TypeKind;
  params?: string[]; // for functions
  paramTypes?: (TypeKind | TypeInfo)[];
  hasRest?: boolean;
  returnType?: TypeInfo;
  members?: Map<string, TypeInfo>;
  parameterSymbols?: SymbolInfo[];
  properties?: string[]; // legacy list
  receiver?: TypeInfo;
  documentation?: string;
  examples?: string[];
  isMutable?: boolean;
  isClass?: boolean;
  elementType?: TypeInfo;
  unionTypes?: TypeInfo[];
  intersectionTypes?: TypeInfo[];
  name?: string;
  isStrict?: boolean;
  isNative?: boolean;
  value?: any;
  range?: Range;
}

interface SymbolInfo extends TypeInfo {
  hasExplicitReturn?: boolean;
  isExported?: boolean;
}
interface ExportInfo extends TypeInfo {}

type SymbolTable = Map<string, SymbolInfo>;

// Module members: moduleName -> { memberName -> ExportInfo }
type ModuleMemberTable = Map<string, Map<string, ExportInfo>>;
const moduleExportCache: Map<string, Map<string, ExportInfo>> = new Map();

// ─── Token ───────────────────────────────────────────────────────────────────

interface Token {
  type: string;
  value: string;
  range: Range;
}

export interface ParseResult {
  diagnostics: Diagnostic[];
  symbols: SymbolTable;
  tokens: Token[];
  importedModules: string[]; // names of `use`-d modules
  hoverMap: Map<string, TypeInfo>; // line:col -> TypeInfo
}

// ─── All keyword values ───────────────────────────────────────────────────────

const ALL_KEYWORDS = [
  'set', 'var', 'define', 'if', 'else', 'return', 'match', 'default',
  'use', 'export', 'null', 'undefined', 'true', 'false',
  'while', 'for', 'in', 'as', 'from', 'class', 'abstract', 'private', 'readonly', 'this', 'async', 'after', 'every', 'layout', 'typeof',
  'string', 'number', 'boolean', 'array', 'any', 'void', 'bigint', 'regex', 'unknown'
];

const BUILTIN_OBJECTS = ['console', 'process', 'fs', 'string', 'Date', 'Map', 'HashMap', 'regex', 'net', 'http', 'exit', 'Timer', 'sqlite', 'storage', 'JSON'];
const ARRAY_CALLBACK_METHODS = ['map', 'filter', 'find', 'some', 'every', 'forEach', 'findIndex'];

const BUILTIN_MEMBERS: Map<string, Map<string, TypeInfo>> = new Map([
  ['console', new Map<string, TypeInfo>([
    ['show', { kind: TypeKind.Void, params: ['...args'], paramTypes: [TypeKind.Unknown], returnType: { kind: TypeKind.Void }, documentation: 'Output values to the console.' }],
    ['warn', { kind: TypeKind.Void, params: ['...args'], paramTypes: [TypeKind.Unknown], returnType: { kind: TypeKind.Void }, documentation: 'Prints warnings to stdout.' }],
    ['args', { kind: TypeKind.Array, params: [], paramTypes: [], returnType: { kind: TypeKind.Array }, documentation: 'Returns command-line arguments.' }],
    ['read', { kind: TypeKind.String, params: [], paramTypes: [], returnType: { kind: TypeKind.String }, documentation: 'Reads a line from stdin.' }]
  ])],
  ['process', new Map<string, TypeInfo>([
    ['run', { 
      kind: TypeKind.Object, 
      params: ['command', 'args'], 
      paramTypes: [TypeKind.String, TypeKind.Array], 
      returnType: { 
        kind: TypeKind.Object, 
        members: new Map<string, TypeInfo>([
          ['output', { kind: TypeKind.String }],
          ['status', { kind: TypeKind.Number }],
          ['failed', { kind: TypeKind.Boolean }]
        ])
      },
      documentation: 'Executes a command synchronously.' 
    }],
    ['spawn', { 
      kind: TypeKind.Object, 
      params: ['command', 'args'], 
      paramTypes: [TypeKind.String, TypeKind.Array], 
      returnType: { 
        kind: TypeKind.Object, 
        members: new Map<string, TypeInfo>([
          ['spawned', { kind: TypeKind.Boolean }]
        ])
      },
      documentation: 'Starts a background process.' 
    }],
    ['sleep', {
      kind: TypeKind.Void,
      params: ['ms'],
      paramTypes: [TypeKind.Number],
      returnType: { kind: TypeKind.Void },
      documentation: 'Pauses execution for the specified number of milliseconds.'
    }]
  ])],
  ['fs', new Map<string, TypeInfo>([
    ['read', { kind: TypeKind.String, params: ['path'], paramTypes: [TypeKind.String], documentation: 'Reads a file.' }],
    ['info', { 
      kind: TypeKind.Object, 
      params: ['path'], 
      paramTypes: [TypeKind.String], 
      returnType: { 
        kind: TypeKind.Object, 
        documentation: 'Returns an object containing file metadata: path, dirname, name, ext, size, length, and exists.',
        members: new Map<string, TypeInfo>([
          ['path', { kind: TypeKind.String, documentation: 'The full path to the file.' }],
          ['dirname', { kind: TypeKind.String, documentation: 'The directory name of the file.' }],
          ['name', { kind: TypeKind.String, documentation: 'The base name of the file.' }],
          ['ext', { kind: TypeKind.String, documentation: 'The file extension (e.g., ".sp").' }],
          ['size', { kind: TypeKind.Number, documentation: 'The size of the file in bytes.' }],
          ['length', { kind: TypeKind.Number, documentation: 'The size of the file in bytes (alias for size).' }],
          ['exists', { kind: TypeKind.Boolean, documentation: 'Whether the file exists.' }],
          ['modifiedAt', { kind: TypeKind.Date, documentation: 'The last modification time of the file.' }],
          ['createdAt', { kind: TypeKind.Date, documentation: 'The creation time (or status change time) of the file.' }]
        ])
      }
    }],
    ['create', { kind: TypeKind.Object, params: ['path', 'content'], paramTypes: [TypeKind.String, TypeKind.String], returnType: { kind: TypeKind.Object, members: new Map([['error', { kind: TypeKind.String }]]) }, documentation: 'Creates a new file with the specified content.' }],
    ['overwrite', { kind: TypeKind.Unknown, params: ['path', 'content', 'options?'], paramTypes: [TypeKind.String, TypeKind.String, { kind: TypeKind.Object, members: new Map([['line', { kind: TypeKind.Number, documentation: 'The specific line number to overwrite.' }]]) }], returnType: { kind: TypeKind.Void }, documentation: 'Overwrites a file. Use { line: n } in options to overwrite a specific line.' }],
    ['append', { kind: TypeKind.Unknown, params: ['path', 'content', 'options?'], paramTypes: [TypeKind.String, TypeKind.String, { kind: TypeKind.Object, members: new Map([['line', { kind: TypeKind.Number, documentation: 'The specific line number to append at.' }]]) }], returnType: { kind: TypeKind.Void }, documentation: 'Appends content to a file. Use { line: n } in options to append after a specific line.' }],
    ['delete', { kind: TypeKind.Unknown, params: ['path'], paramTypes: [TypeKind.String], returnType: { kind: TypeKind.Void }, documentation: 'Deletes a file.' }],
    ['readJson', { 
      kind: TypeKind.Object, 
      params: ['path'], 
      paramTypes: [TypeKind.String], 
      returnType: { kind: TypeKind.Object },
      documentation: 'Reads and parses a JSON file into an object.' 
    }],
    ['writeJson', { 
      kind: TypeKind.Void, 
      params: ['path', 'value'], 
      paramTypes: [TypeKind.String, TypeKind.Unknown], 
      returnType: { kind: TypeKind.Void },
      documentation: 'Formats and writes an object to a JSON file.' 
    }]
  ])],
  ['Date', new Map<string, TypeInfo>([
    ['now', { kind: TypeKind.Date, params: [], paramTypes: [], returnType: { kind: TypeKind.Date }, documentation: 'Returns the current timestamp as a Date object.' }],
    ['parse', { kind: TypeKind.Date, params: ['str'], paramTypes: [TypeKind.String], returnType: { kind: TypeKind.Date }, documentation: 'Parses a date string.' }],
    ['year', { kind: TypeKind.Number, documentation: 'Returns the year of the date.' }],
    ['month', { kind: TypeKind.Number, documentation: 'Returns the month of the date (1-12).' }],
    ['day', { kind: TypeKind.Number, documentation: 'Returns the day of the date.' }],
    ['hour', { kind: TypeKind.Number, documentation: 'Returns the hour of the date.' }],
    ['minute', { kind: TypeKind.Number, documentation: 'Returns the minute of the date.' }],
    ['second', { kind: TypeKind.Number, documentation: 'Returns the second of the date.' }],
    ['toString', { kind: TypeKind.String, params: [], paramTypes: [], returnType: { kind: TypeKind.String }, documentation: 'Returns a string representation of the date.' }]
  ])],
  ['BigInt', new Map<string, TypeInfo>([
    ['toString', { kind: TypeKind.String, params: [], paramTypes: [], returnType: { kind: TypeKind.String }, documentation: 'Returns a string representation of the BigInt.' }]
  ])],
  ['string', new Map<string, TypeInfo>([
    ['trim', { kind: TypeKind.String, params: [], paramTypes: [], returnType: { kind: TypeKind.String }, documentation: 'Removes whitespace from both ends of a string.' }],
    ['toLowerCase', { kind: TypeKind.String, params: [], paramTypes: [], returnType: { kind: TypeKind.String }, documentation: 'Returns the calling string value converted to lowercase.' }],
    ['toUpperCase', { kind: TypeKind.String, params: [], paramTypes: [], returnType: { kind: TypeKind.String }, documentation: 'Returns the calling string value converted to uppercase.' }],
    ['contains', { kind: TypeKind.Boolean, params: ['searchString'], paramTypes: [TypeKind.String], returnType: { kind: TypeKind.Boolean }, documentation: 'Determines whether one string may be found within another string.' }],
    ['includes', { kind: TypeKind.Boolean, params: ['searchString'], paramTypes: [TypeKind.String], returnType: { kind: TypeKind.Boolean }, documentation: 'Determines whether one string may be found within another string (alias for contains).' }],
    ['startsWith', { kind: TypeKind.Boolean, params: ['searchString'], paramTypes: [TypeKind.String], returnType: { kind: TypeKind.Boolean }, documentation: 'Determines whether a string begins with the characters of a specified string.' }],
    ['endsWith', { kind: TypeKind.Boolean, params: ['searchString'], paramTypes: [TypeKind.String], returnType: { kind: TypeKind.Boolean }, documentation: 'Determines whether a string ends with the characters of a specified string.' }],
    ['indexOf', { kind: TypeKind.Number, params: ['search'], paramTypes: [TypeKind.Unknown], returnType: { kind: TypeKind.Number }, documentation: 'Returns the index within the calling string object of the first occurrence of the specified value.' }],
    ['split', { kind: TypeKind.Array, params: ['separator'], paramTypes: [TypeKind.Unknown], returnType: { kind: TypeKind.Array }, documentation: 'Splits a String object into an array of strings by separating the string into substrings.' }],
    ['replace', { kind: TypeKind.String, params: ['searchValue', 'replaceValue'], paramTypes: [TypeKind.Unknown, TypeKind.String], returnType: { kind: TypeKind.String }, documentation: 'Returns a new string with some or all matches of a pattern replaced by a replacement.' }],
    ['match', { kind: TypeKind.Array, params: ['regex'], paramTypes: [TypeKind.Regex], returnType: { kind: TypeKind.Array }, documentation: 'Returns an array of all matches of a regex in the string.' }],
    ['substring', { kind: TypeKind.String, params: ['indexStart', 'indexEnd'], paramTypes: [TypeKind.Number, TypeKind.Number], returnType: { kind: TypeKind.String }, documentation: 'Returns the part of the string between the start and end indexes, or to the end of the string.' }],
    ['length', { kind: TypeKind.Number, documentation: 'The length of the string.' }],
    ['size', { kind: TypeKind.Number, documentation: 'The length of the string (alias for length).' }],
    ['padStart', { kind: TypeKind.String, params: ['targetLength', 'padString?'], paramTypes: [TypeKind.Number, TypeKind.String], returnType: { kind: TypeKind.String }, documentation: 'Pads the current string with another string until the resulting string reaches the given length.' }],
    ['padEnd', { kind: TypeKind.String, params: ['targetLength', 'padString?'], paramTypes: [TypeKind.Number, TypeKind.String], returnType: { kind: TypeKind.String }, documentation: 'Pads the current string with another string until the resulting string reaches the given length.' }],
    ['repeat', { kind: TypeKind.String, params: ['count'], paramTypes: [TypeKind.Number], returnType: { kind: TypeKind.String }, documentation: 'Returns a string which contains the specified number of copies of the string.' }],
    ['charCodeAt', { kind: TypeKind.Number, params: ['index'], paramTypes: [TypeKind.Number], returnType: { kind: TypeKind.Number }, documentation: 'Returns an integer between 0 and 65535 representing the UTF-16 code unit at the given index.' }],
    ['error', { kind: TypeKind.Unknown, params: ['callback'], paramTypes: [TypeKind.Unknown], documentation: 'Registers an error handler for the string value.' }]
  ])],
  ['Array', new Map<string, TypeInfo>([
    ['push', { kind: TypeKind.Number, params: ['...items'], paramTypes: [TypeKind.Unknown], returnType: { kind: TypeKind.Number }, documentation: 'Adds one or more elements to the end of an array and returns the new length of the array.' }],
    ['pop', { kind: TypeKind.Unknown, params: [], paramTypes: [], documentation: 'Removes the last element from an array and returns that element.' }],
    ['shift', { kind: TypeKind.Unknown, params: [], paramTypes: [], documentation: 'Removes the first element from an array and returns that removed element.' }],
    ['unshift', { kind: TypeKind.Number, params: ['...items'], paramTypes: [TypeKind.Unknown], returnType: { kind: TypeKind.Number }, documentation: 'Adds one or more elements to the beginning of an array and returns the new length of the array.' }],
    ['join', { kind: TypeKind.String, params: ['separator'], paramTypes: [TypeKind.String], returnType: { kind: TypeKind.String }, documentation: 'Joins all elements of an array into a string.' }],
    ['reverse', { kind: TypeKind.Array, params: [], paramTypes: [], returnType: { kind: TypeKind.Array }, documentation: 'Reverses an array in place.' }],
    ['slice', { kind: TypeKind.Array, params: ['start', 'end'], paramTypes: [TypeKind.Number, TypeKind.Number], returnType: { kind: TypeKind.Array }, documentation: 'Returns a shallow copy of a portion of an array into a new array object.' }],
    ['contains', { kind: TypeKind.Boolean, params: ['value'], paramTypes: [TypeKind.Unknown], returnType: { kind: TypeKind.Boolean }, documentation: 'Determines whether an array includes a certain value among its entries.' }],
    ['includes', { kind: TypeKind.Boolean, params: ['value'], paramTypes: [TypeKind.Unknown], returnType: { kind: TypeKind.Boolean }, documentation: 'Determines whether an array includes a certain value among its entries (alias for contains).' }],
    ['indexOf', { kind: TypeKind.Number, params: ['value'], paramTypes: [TypeKind.Unknown], returnType: { kind: TypeKind.Number }, documentation: 'Returns the first index at which a given element can be found in the array.' }],
    ['map', { kind: TypeKind.Array, params: ['callback'], paramTypes: [TypeKind.Unknown], returnType: { kind: TypeKind.Array }, documentation: 'Creates a new array populated with the results of calling a provided function on every element in the calling array.' }],
    ['filter', { kind: TypeKind.Array, params: ['callback'], paramTypes: [TypeKind.Unknown], returnType: { kind: TypeKind.Array }, documentation: 'Creates a new array with all elements that pass the test implemented by the provided function.' }],
    ['forEach', { kind: TypeKind.Void, params: ['callback'], paramTypes: [TypeKind.Unknown], returnType: { kind: TypeKind.Void }, documentation: 'Executes a provided function once for each array element.' }],
    ['reduce', { kind: TypeKind.Unknown, params: ['callback', 'initialValue?'], paramTypes: [TypeKind.Unknown, TypeKind.Unknown], documentation: 'Executes a user-supplied "reducer" callback function on each element of the array, in order, passing in the return value from the calculation on the preceding element.' }],
    ['every', { kind: TypeKind.Boolean, params: ['callback'], paramTypes: [TypeKind.Unknown], returnType: { kind: TypeKind.Boolean }, documentation: 'Tests whether all elements in the array pass the test implemented by the provided function.' }],
    ['some', { kind: TypeKind.Boolean, params: ['callback'], paramTypes: [TypeKind.Unknown], returnType: { kind: TypeKind.Boolean }, documentation: 'Tests whether at least one element in the array passes the test implemented by the provided function.' }],
    ['find', { kind: TypeKind.Unknown, params: ['callback'], paramTypes: [TypeKind.Unknown], documentation: 'Returns the first element in the provided array that satisfies the provided testing function.' }],
    ['findIndex', { kind: TypeKind.Number, params: ['callback'], paramTypes: [TypeKind.Unknown], returnType: { kind: TypeKind.Number }, documentation: 'Returns the index of the first element in an array that satisfies the provided testing function.' }],
    ['flat', { kind: TypeKind.Array, params: ['depth?'], paramTypes: [TypeKind.Number], returnType: { kind: TypeKind.Array }, documentation: 'Creates a new array with all sub-array elements concatenated into it recursively up to the specified depth.' }],
    ['length', { kind: TypeKind.Number, documentation: 'The number of elements in the array.' }],
    ['error', { kind: TypeKind.Array, params: ['callback'], paramTypes: [TypeKind.Unknown], returnType: { kind: TypeKind.Array }, documentation: 'Registers an error handler for the array.' }]
  ])],
  ['Error', new Map<string, TypeInfo>([
    ['message', { 
        kind: TypeKind.String, 
        unionTypes: [{ kind: TypeKind.String }, { kind: TypeKind.Undefined }],
        documentation: 'The message describing the error.' 
    }],
    ['line', { 
        kind: TypeKind.Number, 
        unionTypes: [{ kind: TypeKind.Number }, { kind: TypeKind.Null }],
        documentation: 'The line number where the error occurred.' 
    }]
  ])],
  ['number', new Map<string, TypeInfo>([
    ['toFixed', { kind: TypeKind.String, params: ['digits'], paramTypes: [TypeKind.Number], returnType: { kind: TypeKind.String }, documentation: 'Formats a number using fixed-point notation.' }],
    ['toString', { kind: TypeKind.String, params: [], paramTypes: [], returnType: { kind: TypeKind.String }, documentation: 'Returns a string representing the specified number.' }],
    ['error', { kind: TypeKind.Number, params: ['callback'], paramTypes: [TypeKind.Unknown], returnType: { kind: TypeKind.Number }, documentation: 'Registers an error handler for the number.' }]
  ])],
  ['Object', new Map<string, TypeInfo>([
    ['keys', { kind: TypeKind.Array, params: [], paramTypes: [], returnType: { kind: TypeKind.Array }, documentation: 'Returns an array of a given object\'s own enumerable property names.' }],
    ['values', { kind: TypeKind.Array, params: [], paramTypes: [], returnType: { kind: TypeKind.Array }, documentation: 'Returns an array of a given object\'s own enumerable property values.' }],
    ['entries', { kind: TypeKind.Array, params: [], paramTypes: [], returnType: { kind: TypeKind.Array }, documentation: 'Returns an array of a given object\'s own enumerable string-keyed property [key, value] pairs.' }],
    ['assign', { kind: TypeKind.Object, params: ['target', '...sources'], paramTypes: [TypeKind.Object, TypeKind.Object], returnType: { kind: TypeKind.Object }, documentation: 'Copies all enumerable own properties from one or more source objects to a target object. It returns the modified target object.' }],
    ['has', { kind: TypeKind.Boolean, params: ['key'], paramTypes: [TypeKind.String], returnType: { kind: TypeKind.Boolean }, documentation: 'Returns a boolean indicating whether the object has the specified property as its own property.' }],
    ['error', { kind: TypeKind.Object, params: ['callback'], paramTypes: [TypeKind.Unknown], returnType: { kind: TypeKind.Object }, documentation: 'Registers an error handler for the object.' }]
  ])],
  ['Map', new Map<string, TypeInfo>([
    ['set', { kind: TypeKind.Map, params: ['key', 'value'], paramTypes: [TypeKind.Unknown, TypeKind.Unknown], returnType: { kind: TypeKind.Map }, documentation: 'Sets the value for the key in the Map object. Returns the Map object.' }],
    ['get', { kind: TypeKind.Unknown, params: ['key'], paramTypes: [TypeKind.Unknown], documentation: 'Returns the value associated to the key, or null if there is none.' }],
    ['has', { kind: TypeKind.Boolean, params: ['key'], paramTypes: [TypeKind.Unknown], documentation: 'Returns a boolean asserting whether a value has been associated to the key in the Map object or not.' }],
    ['delete', { kind: TypeKind.Boolean, params: ['key'], paramTypes: [TypeKind.Unknown], documentation: 'Removes any value associated to the key and returns a boolean asserting whether an element was removed or not.' }],
    ['clear', { kind: TypeKind.Void, params: [], paramTypes: [], documentation: 'Removes all key-value pairs from the Map object.' }],
    ['size', { kind: TypeKind.Number, documentation: 'Returns the number of key-value pairs in the Map object.' }],
    ['keys', { kind: TypeKind.Array, params: [], paramTypes: [], returnType: { kind: TypeKind.Array }, documentation: 'Returns a new Array object that contains the keys for each element in the Map object.' }],
    ['values', { kind: TypeKind.Array, params: [], paramTypes: [], returnType: { kind: TypeKind.Array }, documentation: 'Returns a new Array object that contains the values for each element in the Map object.' }],
    ['forEach', { kind: TypeKind.Void, params: ['callback'], paramTypes: [TypeKind.Unknown], documentation: 'Executes a provided function once for each key/value pair in the Map object.' }]
  ])],
  ['HashMap', new Map<string, TypeInfo>([
    // Alias to Map handled in resolution
  ])],
  ['regex_builder', new Map<string, TypeInfo>([
    ['start', { kind: TypeKind.Regex, name: 'regex_builder_started', params: [], returnType: { kind: TypeKind.Regex, name: 'regex_builder_started' }, documentation: 'Matches the start of the string (^).' }],
    ['digit', { kind: TypeKind.Regex, name: 'regex_builder', params: [], returnType: { kind: TypeKind.Regex, name: 'regex_builder' }, documentation: 'Matches any digit character (\\d).' }],
    ['nonDigit', { kind: TypeKind.Regex, name: 'regex_builder', params: [], returnType: { kind: TypeKind.Regex, name: 'regex_builder' }, documentation: 'Matches any non-digit character (\\D).' }],
    ['word', { kind: TypeKind.Regex, name: 'regex_builder', params: [], returnType: { kind: TypeKind.Regex, name: 'regex_builder' }, documentation: 'Matches any word character (\\w).' }],
    ['letter', { kind: TypeKind.Regex, name: 'regex_builder', params: [], returnType: { kind: TypeKind.Regex, name: 'regex_builder' }, documentation: 'Matches any letter (a-zA-Z).' }],
    ['whitespace', { kind: TypeKind.Regex, name: 'regex_builder', params: [], returnType: { kind: TypeKind.Regex, name: 'regex_builder' }, documentation: 'Matches any whitespace character (\\s).' }],
    ['any', { kind: TypeKind.Regex, name: 'regex_builder', params: [], returnType: { kind: TypeKind.Regex, name: 'regex_builder' }, documentation: 'Matches any single character except newline (.).' }],
    ['end', { kind: TypeKind.Regex, name: 'regex_final', params: [], returnType: { kind: TypeKind.Regex, name: 'regex_final' }, documentation: 'Matches the end of the string ($).' }],
    ['wordBoundary', { kind: TypeKind.Regex, name: 'regex_builder', params: [], returnType: { kind: TypeKind.Regex, name: 'regex_builder' }, documentation: 'Matches a word boundary (\\b).' }],
    ['text', { kind: TypeKind.Regex, name: 'regex_builder', params: ['str'], paramTypes: [TypeKind.String], returnType: { kind: TypeKind.Regex, name: 'regex_builder' }, documentation: 'Matches the literal text provided (escaped).' }],
    ['range', { kind: TypeKind.Regex, name: 'regex_builder', params: ['start', 'end'], paramTypes: [TypeKind.String, TypeKind.String], returnType: { kind: TypeKind.Regex, name: 'regex_builder' }, documentation: 'Matches a range of characters [a-z].' }],
    ['repeat', { kind: TypeKind.Regex, name: 'regex_builder', params: ['count'], paramTypes: [TypeKind.Number], returnType: { kind: TypeKind.Regex, name: 'regex_builder' }, documentation: 'Repeats the previous element exactly n times.' }],
    ['repeatAtLeast', { kind: TypeKind.Regex, name: 'regex_builder', params: ['min'], paramTypes: [TypeKind.Number], returnType: { kind: TypeKind.Regex, name: 'regex_builder' }, documentation: 'Repeats the previous element at least n times.' }],
    ['repeatBetween', { kind: TypeKind.Regex, name: 'regex_builder', params: ['min', 'max'], paramTypes: [TypeKind.Number, TypeKind.Number], returnType: { kind: TypeKind.Regex, name: 'regex_builder' }, documentation: 'Repeats the previous element between min and max times.' }],
    ['maybe', { kind: TypeKind.Regex, name: 'regex_builder', params: [], returnType: { kind: TypeKind.Regex, name: 'regex_builder' }, documentation: 'Makes the previous element optional (?).' }],
    ['optional', { kind: TypeKind.Regex, name: 'regex_builder', params: [], returnType: { kind: TypeKind.Regex, name: 'regex_builder' }, documentation: 'Makes the previous element optional (alias for maybe).' }],
    ['oneOrMore', { kind: TypeKind.Regex, name: 'regex_builder', params: [], returnType: { kind: TypeKind.Regex, name: 'regex_builder' }, documentation: 'Matches the previous element one or more times (+).' }],
    ['zeroOrMore', { kind: TypeKind.Regex, name: 'regex_builder', params: [], returnType: { kind: TypeKind.Regex, name: 'regex_builder' }, documentation: 'Matches the previous element zero or more times (*).' }],
    ['capture', { kind: TypeKind.Regex, name: 'regex_builder', params: ['inner'], paramTypes: [TypeKind.Regex], returnType: { kind: TypeKind.Regex, name: 'regex_builder' }, documentation: 'Creates a capturing group around the pattern.' }],
    ['group', { kind: TypeKind.Regex, name: 'regex_builder', params: ['inner'], paramTypes: [TypeKind.Regex], returnType: { kind: TypeKind.Regex, name: 'regex_builder' }, documentation: 'Creates a non-capturing group around the pattern.' }],
    ['or', { kind: TypeKind.Regex, name: 'regex_builder', params: ['other'], paramTypes: [TypeKind.Regex], returnType: { kind: TypeKind.Regex, name: 'regex_builder' }, documentation: 'Matches either the current pattern or the provided pattern (|).' }],
    ['test', { kind: TypeKind.Boolean, params: ['str'], paramTypes: [TypeKind.String], returnType: { kind: TypeKind.Boolean }, documentation: 'Tests if the regex matches the provided string.' }]
  ])],
  ['regex_builder_started', new Map<string, TypeInfo>([
    ['digit', { kind: TypeKind.Regex, name: 'regex_builder_started', params: [], returnType: { kind: TypeKind.Regex, name: 'regex_builder_started' }, documentation: 'Matches any digit character (\\d).' }],
    ['nonDigit', { kind: TypeKind.Regex, name: 'regex_builder_started', params: [], returnType: { kind: TypeKind.Regex, name: 'regex_builder_started' }, documentation: 'Matches any non-digit character (\\D).' }],
    ['word', { kind: TypeKind.Regex, name: 'regex_builder_started', params: [], returnType: { kind: TypeKind.Regex, name: 'regex_builder_started' }, documentation: 'Matches any word character (\\w).' }],
    ['letter', { kind: TypeKind.Regex, name: 'regex_builder_started', params: [], returnType: { kind: TypeKind.Regex, name: 'regex_builder_started' }, documentation: 'Matches any letter (a-zA-Z).' }],
    ['whitespace', { kind: TypeKind.Regex, name: 'regex_builder_started', params: [], returnType: { kind: TypeKind.Regex, name: 'regex_builder_started' }, documentation: 'Matches any whitespace character (\\s).' }],
    ['any', { kind: TypeKind.Regex, name: 'regex_builder_started', params: [], returnType: { kind: TypeKind.Regex, name: 'regex_builder_started' }, documentation: 'Matches any single character except newline (.).' }],
    ['end', { kind: TypeKind.Regex, name: 'regex_final', params: [], returnType: { kind: TypeKind.Regex, name: 'regex_final' }, documentation: 'Matches the end of the string ($).' }],
    ['wordBoundary', { kind: TypeKind.Regex, name: 'regex_builder_started', params: [], returnType: { kind: TypeKind.Regex, name: 'regex_builder_started' }, documentation: 'Matches a word boundary (\\b).' }],
    ['text', { kind: TypeKind.Regex, name: 'regex_builder_started', params: ['str'], paramTypes: [TypeKind.String], returnType: { kind: TypeKind.Regex, name: 'regex_builder_started' }, documentation: 'Matches the literal text provided (escaped).' }],
    ['range', { kind: TypeKind.Regex, name: 'regex_builder_started', params: ['start', 'end'], paramTypes: [TypeKind.String, TypeKind.String], returnType: { kind: TypeKind.Regex, name: 'regex_builder_started' }, documentation: 'Matches a range of characters [a-z].' }],
    ['repeat', { kind: TypeKind.Regex, name: 'regex_builder_started', params: ['count'], paramTypes: [TypeKind.Number], returnType: { kind: TypeKind.Regex, name: 'regex_builder_started' }, documentation: 'Repeats the previous element exactly n times.' }],
    ['repeatAtLeast', { kind: TypeKind.Regex, name: 'regex_builder_started', params: ['min'], paramTypes: [TypeKind.Number], returnType: { kind: TypeKind.Regex, name: 'regex_builder_started' }, documentation: 'Repeats the previous element at least n times.' }],
    ['repeatBetween', { kind: TypeKind.Regex, name: 'regex_builder_started', params: ['min', 'max'], paramTypes: [TypeKind.Number, TypeKind.Number], returnType: { kind: TypeKind.Regex, name: 'regex_builder_started' }, documentation: 'Repeats the previous element between min and max times.' }],
    ['maybe', { kind: TypeKind.Regex, name: 'regex_builder_started', params: [], returnType: { kind: TypeKind.Regex, name: 'regex_builder_started' }, documentation: 'Makes the previous element optional (?).' }],
    ['optional', { kind: TypeKind.Regex, name: 'regex_builder_started', params: [], returnType: { kind: TypeKind.Regex, name: 'regex_builder_started' }, documentation: 'Makes the previous element optional (alias for maybe).' }],
    ['oneOrMore', { kind: TypeKind.Regex, name: 'regex_builder_started', params: [], returnType: { kind: TypeKind.Regex, name: 'regex_builder_started' }, documentation: 'Matches the previous element one or more times (+).' }],
    ['zeroOrMore', { kind: TypeKind.Regex, name: 'regex_builder_started', params: [], returnType: { kind: TypeKind.Regex, name: 'regex_builder_started' }, documentation: 'Matches the previous element zero or more times (*).' }],
    ['capture', { kind: TypeKind.Regex, name: 'regex_builder_started', params: ['inner'], paramTypes: [TypeKind.Regex], returnType: { kind: TypeKind.Regex, name: 'regex_builder_started' }, documentation: 'Creates a capturing group around the pattern.' }],
    ['group', { kind: TypeKind.Regex, name: 'regex_builder_started', params: ['inner'], paramTypes: [TypeKind.Regex], returnType: { kind: TypeKind.Regex, name: 'regex_builder_started' }, documentation: 'Creates a non-capturing group around the pattern.' }],
    ['or', { kind: TypeKind.Regex, name: 'regex_builder_started', params: ['other'], paramTypes: [TypeKind.Regex], returnType: { kind: TypeKind.Regex, name: 'regex_builder_started' }, documentation: 'Matches either the current pattern or the provided pattern (|).' }],
    ['test', { kind: TypeKind.Boolean, params: ['str'], paramTypes: [TypeKind.String], returnType: { kind: TypeKind.Boolean }, documentation: 'Tests if the regex matches the provided string.' }]
  ])],
  ['regex_final', new Map<string, TypeInfo>([
    ['test', { kind: TypeKind.Boolean, params: ['str'], paramTypes: [TypeKind.String], returnType: { kind: TypeKind.Boolean }, documentation: 'Tests if the regex matches the provided string.' }],
    ['global', { kind: TypeKind.Regex, name: 'regex_final', params: [], returnType: { kind: TypeKind.Regex, name: 'regex_final' }, documentation: 'Sets the regex to perform a global search/replace.' }]
  ])],
  ['net', new Map<string, TypeInfo>([
    ['get', { kind: TypeKind.Object, params: ['url'], paramTypes: [TypeKind.String], returnType: { kind: TypeKind.Object, name: 'Response' }, documentation: 'Performs an HTTP GET request.' }],
    ['post', { kind: TypeKind.Object, params: ['url', 'body'], paramTypes: [TypeKind.String, TypeKind.Unknown], returnType: { kind: TypeKind.Object, name: 'Response' }, documentation: 'Performs an HTTP POST request.' }],
    ['serve', { 
      kind: TypeKind.Void, 
      params: ['port', 'handler'], 
      paramTypes: [
        TypeKind.Number, 
        { 
          kind: TypeKind.Function, 
          params: ['req'], 
          paramTypes: [{ kind: TypeKind.Object, name: 'Request' }], 
          returnType: { kind: TypeKind.Unknown }
        }
      ], 
      documentation: 'Starts an HTTP server on the specified port.' 
    }]
  ])],
  ['http', new Map<string, TypeInfo>([
    ['get', { kind: TypeKind.Object, params: ['url'], paramTypes: [TypeKind.String], returnType: { kind: TypeKind.Object, name: 'Response' }, documentation: 'Performs an HTTP GET request (alias for net.get).' }],
    ['post', { kind: TypeKind.Object, params: ['url', 'body'], paramTypes: [TypeKind.String, TypeKind.Unknown], returnType: { kind: TypeKind.Object, name: 'Response' }, documentation: 'Performs an HTTP POST request (alias for net.post).' }],
    ['serve', { 
      kind: TypeKind.Void, 
      params: ['port', 'handler'], 
      paramTypes: [
        TypeKind.Number, 
        { 
          kind: TypeKind.Function, 
          params: ['req'], 
          paramTypes: [{ kind: TypeKind.Object, name: 'Request' }], 
          returnType: { kind: TypeKind.Unknown }
        }
      ], 
      documentation: 'Starts an HTTP server on the specified port (alias for net.serve).' 
    }]
  ])],
  ['Response', new Map<string, TypeInfo>([
    ['status', { kind: TypeKind.Number, documentation: 'The HTTP status code of the response.' }],
    ['body', { kind: TypeKind.String, documentation: 'The raw response body as a string.' }],
    ['headers', { kind: TypeKind.Object, documentation: 'An object containing the response headers.' }],
    ['json', { kind: TypeKind.Unknown, params: [], paramTypes: [], documentation: 'Parses the response body as JSON and returns an SP object.' }]
  ])],
  ['Request', new Map<string, TypeInfo>([
    ['method', { kind: TypeKind.String, documentation: 'The HTTP method (GET, POST, etc.).' }],
    ['path', { kind: TypeKind.String, documentation: 'The request path.' }],
    ['headers', { kind: TypeKind.Object, documentation: 'An object containing the request headers.' }],
    ['query', { kind: TypeKind.Object, documentation: 'An object containing the URL query parameters.' }],
    ['body', { kind: TypeKind.String, documentation: 'The raw request body as a string.' }]
  ])],
  ['Future', new Map<string, TypeInfo>([
    ['wait', { kind: TypeKind.Unknown, params: [], paramTypes: [], documentation: 'Synchronously waits for the future to complete and returns the result.' }],
    ['error', { kind: TypeKind.Object, name: 'Future', params: ['callback'], paramTypes: [TypeKind.Unknown], returnType: { kind: TypeKind.Object, name: 'Future' }, documentation: 'Registers a callback to handle errors occurring during future execution.' }]
  ])],
  ['Timer', new Map<string, TypeInfo>([
    ['stop', { kind: TypeKind.Void, params: [], paramTypes: [], returnType: { kind: TypeKind.Void }, documentation: 'Stops the repeating timer.' }]
  ])],
  ['exit', new Map<string, TypeInfo>([
    ['', { kind: TypeKind.Void, params: ['code?'], paramTypes: [TypeKind.Number], documentation: 'Terminates the process with an optional exit code.' }]
  ])],
  ['sqlite', new Map<string, TypeInfo>([
    ['open', { kind: TypeKind.Object, name: 'Database', params: ['filename'], paramTypes: [TypeKind.String], returnType: { kind: TypeKind.Object, name: 'Database' }, documentation: 'Opens a SQLite database at the specified path.' }]
  ])],
  ['Database', new Map<string, TypeInfo>([
    ['execute', { kind: TypeKind.Number, params: ['sql', '...params'], paramTypes: [TypeKind.String, TypeKind.Unknown], returnType: { kind: TypeKind.Number }, documentation: 'Executes a non-query SQL statement (INSERT, UPDATE, DELETE, CREATE). Returns the number of affected rows.' }],
    ['query', { kind: TypeKind.Array, params: ['sql', '...params'], paramTypes: [TypeKind.String, TypeKind.Unknown], returnType: { kind: TypeKind.Array }, documentation: 'Executes a SELECT statement. Returns an array of objects representing the rows.' }],
    ['close', { kind: TypeKind.Void, params: [], paramTypes: [], returnType: { kind: TypeKind.Void }, documentation: 'Closes the database connection.' }]
  ])],
  ['JSON', new Map<string, TypeInfo>([
    ['stringify', { kind: TypeKind.String, params: ['value'], paramTypes: [TypeKind.Unknown], returnType: { kind: TypeKind.String }, documentation: 'Converts an SP value to a JSON string.' }],
    ['parse', { kind: TypeKind.Unknown, params: ['text'], paramTypes: [TypeKind.String], documentation: 'Parses a JSON string and returns the corresponding SP value.' }]
  ])],
  ['storage', new Map<string, TypeInfo>([
    ['getItem', { kind: TypeKind.Object, name: 'Future', params: ['key'], paramTypes: [TypeKind.String], returnType: { kind: TypeKind.Object, name: 'Future', returnType: { kind: TypeKind.Unknown } }, documentation: 'Asynchronously retrieves the value for the given key.' }],
    ['setItem', { kind: TypeKind.Object, name: 'Future', params: ['key', 'value'], paramTypes: [TypeKind.String, TypeKind.Unknown], returnType: { kind: TypeKind.Object, name: 'Future', returnType: { kind: TypeKind.Unknown } }, documentation: 'Asynchronously stores a value for the given key.' }],
    ['removeItem', { kind: TypeKind.Object, name: 'Future', params: ['key'], paramTypes: [TypeKind.String], returnType: { kind: TypeKind.Object, name: 'Future', returnType: { kind: TypeKind.Unknown } }, documentation: 'Asynchronously removes the key and its value from storage.' }],
    ['clear', { kind: TypeKind.Object, name: 'Future', params: [], paramTypes: [], returnType: { kind: TypeKind.Object, name: 'Future', returnType: { kind: TypeKind.Unknown } }, documentation: 'Asynchronously clears all keys and values from storage.' }]
  ])]
]);

function getTypeProperties(kind: TypeKind, properties?: string[]): string[] {
  const base = properties || [];
  if (kind === TypeKind.Array || kind === TypeKind.String) {
    if (!base.includes('length')) return [...base, 'length'];
  }
  return base;
}

function mergeTypes(a: TypeInfo, b: TypeInfo): TypeInfo {
  // Handle falsy or placeholder types
  if (!a || (a.kind === TypeKind.Unknown && !(a.unionTypes?.length))) return b;
  if (!b || (b.kind === TypeKind.Unknown && !(b.unionTypes?.length))) return a;

  // 1. Structural Merge for same kinds (useful for Tuples/Objects)
  if (a.kind === b.kind && !a.unionTypes && !b.unionTypes) {
    if (a.members || b.members || a.elementType || b.elementType) {
      const res = { ...a };
      const mergedMembers = new Map(a.members || []);
      if (b.members) {
        for (const [k, v] of b.members.entries()) {
          const existing = mergedMembers.get(k);
          mergedMembers.set(k, existing ? mergeTypes(existing, v) : v);
        }
      }
      res.members = mergedMembers;
      if (a.elementType || b.elementType) {
        res.elementType = (a.elementType && b.elementType) 
          ? mergeTypes(a.elementType, b.elementType) 
          : (a.elementType || b.elementType);
      }
      return res;
    }
    return a; // Same type, no structure to merge
  }

  // 2. Union Logic (different kinds or already unions)
  const union: TypeInfo[] = [];
  const add = (t: TypeInfo) => {
    if (t.unionTypes) t.unionTypes.forEach(add);
    else if (!union.some(u => {
      // Conservative deduplication: only same kind AND same name/structure
      if (u.kind !== t.kind || u.name !== t.name) return false;
      // If it's a complex structure, keep both in the union unless we have a deep compare (simplified to just keep them)
      if (u.members || t.members || u.elementType || t.elementType) return false;
      return true;
    })) union.push(t);
  };
  
  add(a);
  add(b);

  if (union.length === 1) return union[0];
  return { kind: TypeKind.Unknown, unionTypes: union };
}

// ─── Initialize ───────────────────────────────────────────────────────────────

connection.onInitialize((_params: InitializeParams): InitializeResult => {
  connection.console.log('[SP] Language server initialized');
  return {
    capabilities: {
      textDocumentSync: TextDocumentSyncKind.Incremental,
      completionProvider: {
        resolveProvider: false,
        triggerCharacters: ['.', '@']
      },
      signatureHelpProvider: {
        triggerCharacters: ['(', ',']
      },
      hoverProvider: true,
      definitionProvider: true,
      documentSymbolProvider: true,
      codeActionProvider: true
    }
  };
});

connection.onCodeAction((params) => {
  const uri = params.textDocument.uri;
  const doc = documents.get(uri);
  if (!doc) return [];
  
  const result = documentResults.get(uri);
  const actions: CodeAction[] = [];

  for (const diagnostic of params.context.diagnostics) {
    if (diagnostic.data && (diagnostic.data as any).expectedType) {
      const data = diagnostic.data as any;
      
      // Fix 1: Change type label
      if (data.annotatedRange) {
        actions.push({
          title: `Change type to '${data.foundType}'`,
          kind: CodeActionKind.QuickFix,
          diagnostics: [diagnostic],
          edit: {
            changes: {
              [uri]: [
                TextEdit.replace(data.annotatedRange, `: ${data.foundType}`)
              ]
            }
          }
        });
      }

      // Fix 2: Convert literal to string (if applicable)
      if (data.valueRange && data.expectedType === 'string' && data.foundType === 'number') {
        const originalVal = doc.getText(data.valueRange);
        actions.push({
            title: `Convert to string literal`,
            kind: CodeActionKind.QuickFix,
            diagnostics: [diagnostic],
            edit: {
                changes: {
                    [uri]: [
                        TextEdit.replace(data.valueRange, `"${originalVal}"`)
                    ]
                }
            }
        });
      }

      // Fix 3: Change var/define to set (redeclaration)
      if (data.isRedeclaration && data.kwRange) {
          actions.push({
              title: `Change '${data.name}' declaration to assignment ('set')`,
              kind: CodeActionKind.QuickFix,
              diagnostics: [diagnostic],
              edit: {
                  changes: {
                      [uri]: [
                          TextEdit.replace(data.kwRange, 'set')
                      ]
                  }
              }
          });
      }

      // Fix 4: Add explicit type annotation
      if (data.canAddType && data.insertPos) {
          actions.push({
              title: `Explicitly type '${diagnostic.message.split("'")[1]}' as '${data.typeToAdd}'`,
              kind: CodeActionKind.QuickFix,
              diagnostics: [diagnostic],
              edit: {
                  changes: {
                      [uri]: [
                          TextEdit.insert(data.insertPos, `: ${data.typeToAdd}`)
                      ]
                  }
              }
          });
      }
    }

    if (diagnostic.message.includes('Undefined identifier')) {
      const match = diagnostic.message.match(/'([^']+)'/);
      if (match) {
        const name = match[1];

        // Fix 1: Declare as variable
        actions.push({
          title: `Declare '${name}' as variable`,
          kind: CodeActionKind.QuickFix,
          diagnostics: [diagnostic],
          edit: {
            changes: {
              [uri]: [
                TextEdit.insert({ line: 0, character: 0 }, `set ${name} = null\n`)
              ]
            }
          }
        });

        // Fix 2: Add use statement (if it looks like a module name)
        if (/^[a-z]/.test(name)) {
          actions.push({
            title: `Add 'use ${name}'`,
            kind: CodeActionKind.QuickFix,
            diagnostics: [diagnostic],
            edit: {
              changes: {
                [uri]: [
                  TextEdit.insert({ line: 0, character: 0 }, `use ${name}\n`)
                ]
              }
            }
          });
        }
      }
    }
  }
  return actions;
});

// ─── Per-document state ───────────────────────────────────────────────────────

const documentResults: Map<string, ParseResult> = new Map();
const lastParseResult: ParseResult | null = null; // for debugging if needed
const moduleMembers: ModuleMemberTable = new Map(); // moduleName -> members

// ─── Doc Comment Parsing ───────────────────────────────────────────────────

function jsonToType(obj: any): TypeInfo {
  if (typeof obj === 'number') return { kind: TypeKind.Number };
  if (typeof obj === 'string') return { kind: TypeKind.String };
  if (typeof obj === 'boolean') return { kind: TypeKind.Boolean };
  if (obj === null) return { kind: TypeKind.Null };
  if (Array.isArray(obj)) {
    const elementType = obj.length > 0 ? jsonToType(obj[0]) : { kind: TypeKind.Unknown };
    return { kind: TypeKind.Array, elementType };
  }
  if (typeof obj === 'object') {
    const members = new Map<string, TypeInfo>();
    for (const [k, v] of Object.entries(obj)) {
      members.set(k, jsonToType(v));
    }
    return { kind: TypeKind.Object, members };
  }
  return { kind: TypeKind.Unknown };
}

function stringToTypeKind(s: string): TypeKind {
  const lower = s.toLowerCase();
  switch (lower) {
    case 'number': return TypeKind.Number;
    case 'string': return TypeKind.String;
    case 'boolean': return TypeKind.Boolean;
    case 'array': return TypeKind.Array;
    case 'object': return TypeKind.Object;
    case 'map': return TypeKind.Map;
    case 'date': return TypeKind.Date;
    case 'regex': return TypeKind.Regex;
    case 'void': return TypeKind.Void;
    case 'error': return TypeKind.Error;
    case 'function': return TypeKind.Function;
    case 'bigint': return TypeKind.BigInt;
    case 'null': return TypeKind.Null;
    case 'undefined': return TypeKind.Undefined;
    default: return TypeKind.Unknown;
  }
}

function parseDocParamTypes(doc: string): Map<string, TypeInfo> {
  const result = new Map<string, TypeInfo>();
  if (!doc) return result;
  
  // RegEx to find @param {type} name or @param name {type}
  // This matches: @param {number} x  or  @param x {number}
  const paramRegex = /@param\s+(?:\{([^}]+)\}\s+([a-zA-Z0-9_]+)|([a-zA-Z0-9_]+)\s+\{([^}]+)\})/g;
  let match;
  while ((match = paramRegex.exec(doc)) !== null) {
      const typeStr = match[1] || match[4];
      const paramName = match[2] || match[3];
      if (typeStr && paramName) {
          result.set(paramName, { kind: stringToTypeKind(typeStr), name: typeStr });
      }
  }
  return result;
}

interface DocInfo {
  description: string;
  params: Map<string, { type: TypeKind; description?: string }>;
  returnType?: TypeKind;
  examples: string[];
}

function parseDocComment(doc: string): DocInfo {
  const params = new Map<string, { type: TypeKind; description?: string }>();
  let returnType: TypeKind | undefined;
  const examples: string[] = [];
  let descriptionLines: string[] = [];

  const lines = doc.split(/\r?\n/);
  let currentTag: string | null = null;

  for (const line of lines) {
    const trimmed = line.trim();
    if (trimmed.startsWith('@param')) {
      currentTag = 'param';
      const m = trimmed.match(/@param\s+\{([^}]+)\}\s+([a-zA-Z_]\w*)(?:\s+(.*))?/);
      if (m) params.set(m[2], { type: stringToTypeKind(m[1]), description: m[3] });
    } else if (trimmed.startsWith('@return') || trimmed.startsWith('@returns')) {
      currentTag = 'return';
      const m = trimmed.match(/@(return|returns)\s+\{([^}]+)\}(?:\s+(.*))?/);
      if (m) returnType = stringToTypeKind(m[2]);
    } else if (trimmed.startsWith('@example')) {
      currentTag = 'example';
      const ex = trimmed.substring(8).trim();
      examples.push(ex);
    } else if (trimmed.startsWith('@')) {
      currentTag = 'other';
    } else {
      if (currentTag === 'example' && examples.length > 0) {
        examples[examples.length - 1] += '\n' + trimmed;
      } else if (!currentTag) {
        descriptionLines.push(trimmed);
      }
    }
  }

  return {
    description: descriptionLines.filter(l => l.length > 0).join('\n'),
    params,
    returnType,
    examples: examples.map(e => e.trim()).filter(e => e.length > 0)
  };
}





// ─── Completion ───────────────────────────────────────────────────────────────

connection.onCompletion((params: CompletionParams): CompletionItem[] => {
  const uri = params.textDocument.uri;
  const doc = documents.get(uri);
  if (!doc) return [];

  const text = doc.getText();
  const offset = doc.offsetAt(params.position);
  
  // Tag auto-complete for doc comments
  if (params.context?.triggerCharacter === '@') {
    // Check if we are inside a doc comment /** ... */
    const before = text.substring(0, offset);
    const lastStart = before.lastIndexOf('/**');
    const lastEnd = before.lastIndexOf('*/');
    if (lastStart > lastEnd) {
      return [
        { label: 'Array', kind: CompletionItemKind.Class, detail: 'Built-in recursive list type', documentation: 'Recursive container for values of the same type. Usage: Array<T>' },
        { label: 'Error', kind: CompletionItemKind.Class, detail: 'Functional error type', documentation: 'Used for functional error handling. Can be destructured: [res, err] = func()' },
        { label: 'param', kind: CompletionItemKind.Keyword, detail: '@param {type} name description', insertText: 'param {${1:type}} ${2:name} ${3:description}', insertTextFormat: InsertTextFormat.Snippet },
        { label: 'console.show', kind: CompletionItemKind.Function, detail: 'console.show(any...)', insertText: 'console.show($1)', insertTextFormat: InsertTextFormat.Snippet, documentation: 'Print one or more values to the standard output.' },
        { label: 'return', kind: CompletionItemKind.Keyword, detail: '@return {type} description', insertText: 'return {${1:type}} ${2:description}', insertTextFormat: InsertTextFormat.Snippet },
        { label: 'example', kind: CompletionItemKind.Keyword, detail: '@example <code>', insertText: 'example\n * ${1:code}', insertTextFormat: InsertTextFormat.Snippet }
      ];
    }
  }

  const result = getParsedResult(uri);
  if (!result) return [];
  const symbols = result.symbols;
  const importedModules = result.importedModules || [];

  // Check if we are after a dot
  const lineText = doc.getText({
    start: { line: params.position.line, character: 0 },
    end: params.position
  });
  
  // Type auto-complete (after a colon)
  if (/:\s*$/.test(lineText)) {
    const builtinTypes = [
      'string', 'number', 'boolean', 'array', 'object', 'any', 'void', 'null', 'undefined', 'bigint', 'Error', 'Map', 'Date'
    ];
    return builtinTypes.map(t => ({
      label: t,
      kind: CompletionItemKind.TypeParameter,
      detail: `(type) ${t}`
    }));
  }

  const lastExprMatch = lineText.match(/([a-zA-Z_][a-zA-Z0-9_\.\(\)\[\]"' \-\+\*\/]*)\.$/);
  if (lastExprMatch) {
    const fullExpr = lastExprMatch[1];
    const current = resolveExpressionType(fullExpr, result, { line: params.position.line, character: params.position.character - 1 });

    const items: CompletionItem[] = [];

    // 1. Explicit members
    if (current?.members) {
      for (const [name, info] of current.members) {
        items.push({
          label: name,
          kind: (info.params || info.params === undefined && info.kind === TypeKind.Unknown) ? CompletionItemKind.Method : CompletionItemKind.Property,
          detail: info.params ? `(method) ${name}` : `(property) ${name}`,
          documentation: info.documentation ? { kind: 'markdown', value: info.documentation } : undefined
        });
      }
    }

    // 2. Builtin members (Date, BigInt, Array, String, Response, Future)
    let builtinName = '';
    if (current?.kind === TypeKind.Date) builtinName = 'Date';
    else if (current?.kind === TypeKind.BigInt) builtinName = 'BigInt';
    else if (current?.kind === TypeKind.Array) builtinName = 'Array';
    else if (current?.kind === TypeKind.String) builtinName = 'string';
    else if (current?.kind === TypeKind.Map) builtinName = 'Map';
    else if (current?.kind === TypeKind.Number) builtinName = 'number';
    else if (current?.kind === TypeKind.Object) {
       if (current.name && BUILTIN_MEMBERS.has(current.name)) {
          builtinName = current.name;
       } else {
          builtinName = 'Object';
       }
    }
    else if (current?.kind === TypeKind.Regex) {
       if (current.name && BUILTIN_MEMBERS.has(current.name)) {
          builtinName = current.name;
       } else {
          builtinName = 'regex_builder';
       }
    }

    if (builtinName && BUILTIN_MEMBERS.has(builtinName)) {
      const isSpecialized = builtinName !== 'Object';
      const bMembers = BUILTIN_MEMBERS.get(builtinName)!;
      for (const [name, info] of bMembers) {
        // If we are showing a specialized type like Response, suppress existing items if they collide
        // or just prioritize them if they don't.
        if (!items.find(i => i.label === name)) {
          items.push({
            label: name,
            kind: info.params ? CompletionItemKind.Method : CompletionItemKind.Property,
            detail: info.params ? `(method) ${name}` : `(property) ${name}`,
            documentation: info.documentation ? { kind: 'markdown', value: info.documentation } : undefined
          });
        }
      }
      
      // If we ARE specialized, we might want to HIDE the generic Object methods to reduce noise
      if (isSpecialized) {
         // Optionally filter out 'assign', 'keys', 'values' etc if it's a specialized type
         // The user specifically asked to remove things that shouldn't appear
         const genericObjectKeys = Array.from(BUILTIN_MEMBERS.get('Object')!.keys());
         return items.filter(i => !genericObjectKeys.includes(i.label) || i.label === 'error');
      }
    }

    // Special case for length property if not already added
    if ((current?.kind === TypeKind.Array || current?.kind === TypeKind.String) && !items.find(i => i.label === 'length')) {
      items.push({ label: 'length', kind: CompletionItemKind.Property, detail: '(property) length: number' });
    }

    return items;
  }

  // Object key auto-complete (inside an object literal as a function argument)
  // Scan backwards to find { and then ( to find the function call
  let braceDepth = 0;
  let parenDepth = 0;
  let inObject = false;
  let scanIdx = lineText.length - 1;
  
  // Skip current potential identifier
  while (scanIdx >= 0 && /[\w_]/.test(lineText[scanIdx])) scanIdx--;

  while (scanIdx >= 0) {
    const ch = lineText[scanIdx];
    if (ch === '}') braceDepth++;
    else if (ch === '{') {
      if (braceDepth === 0) {
        inObject = true;
        break;
      }
      braceDepth--;
    }
    scanIdx--;
  }

  if (inObject) {
    // Look for the call paren (
    let parenScan = scanIdx - 1;
    while (parenScan >= 0) {
      const ch = lineText[parenScan];
      if (ch === ')') parenDepth++;
      else if (ch === '(') {
        if (parenDepth === 0) {
          // Found potential call!
          const beforeParen = lineText.substring(0, parenScan).trimEnd();
          const exprMatch = beforeParen.match(/([a-zA-Z_][a-zA-Z0-9_\.\[\]\(\)]*)$/);
          if (exprMatch) {
            // Count arguments between ( and the target object literal {
            let argCounter = 0;
            let bracketDepth = 0;
            let pDepth = 0;
            for (let i = parenScan + 1; i < scanIdx; i++) {
                const c = lineText[i];
                if (c === '(') pDepth++;
                else if (c === ')') pDepth--;
                else if (c === '[') bracketDepth++;
                else if (c === ']') bracketDepth--;
                else if (c === ',' && pDepth === 0 && bracketDepth === 0) argCounter++;
            }
            
            const fullExpr = exprMatch[1];
            const funcType = resolveExpressionType(fullExpr, result, { line: params.position.line, character: parenScan });
            if (funcType && funcType.params) {
                const specialized = specializeType(funcType);
                const targetParamValue = specialized.paramTypes ? specialized.paramTypes[argCounter] : undefined;
                if (targetParamValue) {
                    const actualType: TypeInfo = typeof targetParamValue === 'string' ? { kind: targetParamValue } : targetParamValue;
                    if (actualType.kind === TypeKind.Object && actualType.members) {
                        const items: CompletionItem[] = [];
                        for (const [name, info] of actualType.members) {
                            items.push({
                                label: name,
                                kind: CompletionItemKind.Property,
                                detail: `(property) ${name}`,
                                documentation: info.documentation ? { kind: 'markdown', value: info.documentation } : undefined
                            });
                        }
                        return items;
                    }
                }
            }
          }
          break;
        }
        parenDepth--;
      }
      parenScan--;
    }
  }

  // Normal completion: keywords + symbols + built-in objects
  const items: CompletionItem[] = [
    ...ALL_KEYWORDS.map(kw => ({ label: kw, kind: CompletionItemKind.Keyword })),
    ...BUILTIN_OBJECTS.map(obj => ({ label: obj, kind: CompletionItemKind.Variable, detail: `(built-in) ${obj}` }))
  ];

  if (symbols) {
    for (const [name, info] of symbols) {
      if (info.params) {
        const params = info.params;
        const pTypes = info.paramTypes;
        const paramList = params.map((p, i) => `${p}: ${pTypes ? pTypes[i] : 'any'}`).join(', ');
        items.push({
          label: name,
          kind: CompletionItemKind.Function,
          detail: `(function) ${name}(${paramList}): ${info.kind || 'any'}`,
          documentation: info.documentation ? { kind: 'markdown', value: info.documentation } : undefined
        });
      } else {
        items.push({
          label: name,
          kind: CompletionItemKind.Variable,
          detail: `(variable) ${name}: ${info.kind}`,
          documentation: info.documentation ? { kind: 'markdown', value: info.documentation } : undefined
        });
      }
    }
  }

  return items;
});

// ─── Signature Help ───────────────────────────────────────────────────────────

connection.onSignatureHelp((params: SignatureHelpParams): SignatureHelp | null => {
  const uri = params.textDocument.uri;
  const doc = documents.get(uri);
  if (!doc) return null;

  const lineText = doc.getText({
    start: { line: params.position.line, character: 0 },
    end: params.position
  });

  // Walk backwards to find the matching open paren and active param index
  let depth = 0;
  let activeParam = 0;
  let callStart = -1;
  for (let i = lineText.length - 1; i >= 0; i--) {
    const ch = lineText[i];
    if (ch === ')') depth++;
    else if (ch === '(') {
      if (depth === 0) { callStart = i; break; }
      depth--;
    } else if (ch === ',' && depth === 0) {
      activeParam++;
    }
  }
  if (callStart < 0) return null;

  const beforeParen = lineText.substring(0, callStart).trimEnd();
  const result = getParsedResult(uri);
  if (!result) return null;

  // Resolve the expression before the paren to find the function/method being called
  const exprMatch = beforeParen.match(/([a-zA-Z_][a-zA-Z0-9_\.\[\]\(\)]*)$/);
  if (exprMatch) {
    const fullExpr = exprMatch[1];
    const info = resolveExpressionType(fullExpr, result, { line: params.position.line, character: callStart });
    
    if (info && info.params) {
      const specialized = specializeType(info);
      const params = specialized.params || [];
      const pTypes = specialized.paramTypes || [];
      const paramStrings = params.map((p, i) => {
        const pKind = pTypes[i] || { kind: TypeKind.Unknown };
        return `${p}: ${formatType(typeof pKind === 'string' ? { kind: TypeKind.Unknown, name: pKind } : pKind)}`;
      });
      const rType = specialized.returnType ? formatType(specialized.returnType) : 'void';
      const label = `${fullExpr}(${paramStrings.join(', ')}): ${rType}`;
      
      return buildSignature(label, paramStrings, activeParam);
    }
  }

  return null;
});

function buildSignature(label: string, params: string[], activeParam: number): SignatureHelp {
  // Compute parameter offsets within the label precisely
  const parenOpen = label.indexOf('(');
  const parameterInfos: ParameterInformation[] = [];
  let searchFrom = parenOpen + 1;
  for (const p of params) {
    const start = label.indexOf(p, searchFrom);
    if (start === -1) { parameterInfos.push(ParameterInformation.create(p)); continue; }
    const end = start + p.length;
    parameterInfos.push(ParameterInformation.create([start, end]));
    searchFrom = end + 1; // advance past this param so next search doesn't re-match
  }
  const sig: SignatureInformation = { label, parameters: parameterInfos };
  return {
    signatures: [sig],
    activeSignature: 0,
    activeParameter: Math.min(activeParam, Math.max(params.length - 1, 0))
  };
}

// ─── Hover ────────────────────────────────────────────────────────────────────

connection.onHover((params): Hover | null => {
  const uri = params.textDocument.uri;
  const doc = documents.get(uri);
  if (!doc) return null;

  const result = getParsedResult(uri);
  if (!result) return null;

  const pos = params.position;
  const lineText = doc.getText({
    start: { line: pos.line, character: 0 },
    end: { line: pos.line + 1, character: 0 }
  });

  // 1. Try manual word extraction for robustness
  let startIdx = pos.character;
  while (startIdx > 0 && /[\w_]/.test(lineText[startIdx - 1])) startIdx--;
  let endIdx = pos.character;
  while (endIdx < lineText.length && /[\w_]/.test(lineText[endIdx])) endIdx++;
  const word = lineText.substring(startIdx, endIdx).trim();

  // 2. Try Exact Location from hoverMap (Locals/Params)
  const exactKey = `${pos.line}:${startIdx}`;
  const hoverMap = result.hoverMap;
  if (hoverMap && hoverMap.has(exactKey)) {
    const info = hoverMap.get(exactKey)!;
    return { contents: { kind: 'markdown', value: formatDoc(info.documentation, info) } };
  }

  // 3. Try symbol lookup for simple words
  if (word) {
    const KEYWORD_DOCS: Map<string, string> = new Map([
      ['async', 'Starts a block or function as an asynchronous future.'],
      ['after', 'Schedules a task to run once after a delay.'],
      ['every', 'Schedules a task to run periodically.'],
      ['layout', 'Defines a structural type layout or a type alias.'],
      ['set', 'Declares a variable or assigns a value.'],
      ['var', 'Declares a mutable variable.'],
      ['define', 'Defines a function.'],
      ['class', 'Defines a class.'],
      ['if', 'Executes a block of code if a specified condition is true.'],
      ['else', 'Specifies a block of code to be executed if the same condition is false.'],
      ['return', 'Exits the current function and optionally returns a value.'],
      ['match', 'Evaluates an expression and executes a block of code based on matching patterns.'],
      ['typeof', 'Returns the runtime type of the operand as a string.\n\nExample:\ntypeof 42 // "number"\ntypeof "hello" // "string"']
    ]);
    const wordLower = word.toLowerCase();
    if (KEYWORD_DOCS.has(wordLower)) {
      return { contents: { kind: 'markdown', value: `**keyword** ${wordLower}\n\n${KEYWORD_DOCS.get(wordLower)}` } };
    }

    const symbol = result.symbols.get(word);
    if (symbol) {
        return { contents: { kind: 'markdown', value: formatDoc(symbol.documentation, symbol) } };
    }

    // 4. Try Chained Expression resolution
    let exprStart = startIdx;
    while (exprStart > 0 && /[\w_\.\[\]\(\)"' \-\+\*\/]/.test(lineText[exprStart - 1])) exprStart--;
    const fullExpr = lineText.substring(exprStart, endIdx).trim();
    if (fullExpr !== word) {
        const info = resolveExpressionType(fullExpr, result, { line: pos.line, character: endIdx });
        if (info) return { contents: { kind: 'markdown', value: formatDoc(info.documentation, info) } };
    }
  }

  // 5. Fallback: Search all tokens for this position
  const token = result.tokens.find(t => 
    pos.line === t.range.start.line && 
    pos.character >= t.range.start.character && 
    pos.character <= t.range.end.character
  );
  if (token) {
    const info = result.symbols.get(token.value) || resolveExpressionType(token.value, result, { line: pos.line, character: token.range.end.character });
    if (info) return { contents: { kind: 'markdown', value: formatDoc(info.documentation, info) } };
  }
  // 6. Last Ditch: check if we are over a word that is in hoverMap even if startIdx didn't match
  const fuzzyKey = `${pos.line}:`;
  for (const [key, info] of result.hoverMap.entries()) {
    if (key.startsWith(fuzzyKey)) {
        const char = parseInt(key.split(':')[1]);
        if (pos.character >= char && pos.character <= char + word.length) {
            return { contents: { kind: 'markdown', value: formatDoc(info.documentation, info) } };
        }
    }
  }

  return null;
});

connection.onDefinition((params): Definition | null => {
  const uri = params.textDocument.uri;
  const result = getParsedResult(uri);
  if (!result) return null;

  const pos = params.position;
  const doc = documents.get(uri);
  if (!doc) return null;

  const lineText = doc.getText({
    start: { line: pos.line, character: 0 },
    end: { line: pos.line + 1, character: 0 }
  });

  const rangeInfo = getWordRangeAtPosition(lineText, pos.character);
  if (!rangeInfo) return null;

  const { word, start } = rangeInfo;
  
  // 1. Try Exact Location from hoverMap (Locals/Params)
  const exactKey = `${pos.line}:${start}`;
  const info = result.hoverMap.get(exactKey);
  if (info && info.range) {
      return { uri, range: info.range };
  }

  // 2. Try symbol lookup
  const symbol = result.symbols.get(word);
  if (symbol && symbol.range) {
      return { uri, range: symbol.range };
  }

  // 3. Try Chained Expression resolution
  let exprStart = start;
  while (exprStart > 0 && /[\w_\.\[\]\(\)]/.test(lineText[exprStart - 1])) exprStart--;
  const endIdx = rangeInfo.end;
  const fullExpr = lineText.substring(exprStart, endIdx).trim();
  if (fullExpr !== word) {
      const info = resolveExpressionType(fullExpr, result, { line: pos.line, character: endIdx });
      if (info && info.range) return { uri, range: info.range };
  }

  return null;
});

connection.onDocumentSymbol((params): DocumentSymbol[] | null => {
  const uri = params.textDocument.uri;
  const result = getParsedResult(uri);
  if (!result) return null;

  const symbols: DocumentSymbol[] = [];
  for (const [name, info] of result.symbols) {
    if (!info.range) continue;
    symbols.push(DocumentSymbol.create(
      name,
      info.documentation || '',
      info.params ? SymbolKind.Function : (info.isClass ? SymbolKind.Class : SymbolKind.Variable),
      info.range,
      info.range
    ));
  }
  return symbols;
});

// ─── Code Actions (Quick Fixes) ────────────────────────────────────────────────
// Secondary onCodeAction removed (merged into primary)

// ─── Document parsing ─────────────────────────────────────────────────────────

function getWordRangeAtPosition(line: string, character: number): { word: string, start: number, end: number } | null {
  let start = character;
  // If we're on a dot or space, look slightly left
  if (start >= line.length || !/[\w_]/.test(line[start])) {
    if (start > 0 && /[\w_]/.test(line[start - 1])) start--;
    else return null;
  }
  
  let end = start;
  while (start > 0 && /[\w_]/.test(line[start - 1])) start--;
  while (end < line.length && /[\w_]/.test(line[end])) end++;
  if (start === end) return null;
  return { word: line.substring(start, end), start, end };
}

function tokenize(text: string): Token[] {
  const tokens: Token[] = [];
  let i = 0;
  let line = 0;
  let char = 0;

  function advance() {
    const ch = text[i];
    if (ch === '\n') { line++; char = 0; }
    else { char++; }
    i++;
    return ch;
  }

  while (i < text.length) {
    const ch = text[i];

    if (/\s/.test(ch)) { advance(); continue; }

    // comments
    if (ch === '/' && text[i + 1] === '/') {
      advance(); advance();
      while (i < text.length && text[i] !== '\n') advance();
      continue;
    }

    if (ch === '/' && text[i + 1] === '*') {
      const startLine = line;
      const startChar = char;
      const isDoc = text[i + 2] === '*';
      advance(); advance();
      if (isDoc) advance();

      let val = '';
      while (i < text.length && !(text[i] === '*' && text[i + 1] === '/')) {
        val += advance();
      }
      advance(); advance(); // skip */

      if (isDoc) {
        // Clean up doc comment (remove leading * on each line)
        // Keep empty lines to preserve formatting
        const cleaned = val.split(/\r?\n/)
          .map(l => {
            const m = l.match(/^\s*\* ?(.*)$/);
            return m ? m[1] : l.trim();
          })
          .join('\n');
        tokens.push({ type: 'doc_comment', value: cleaned, range: Range.create(startLine, startChar, line, char) });
      }
      continue;
    }

    // strings
    if (ch === '"' || ch === "'") {
      const startLine = line;
      const startChar = char;
      const quote = ch;
      advance();
      let value = '';
      while (i < text.length) {
        if (text[i] === '\\' && i + 1 < text.length) { 
          const next = text[i + 1];
          // Standard escapes
          if (next === quote || next === '\\' || next === 'n' || next === 'r' || next === 't') {
            advance();
            value += advance();
          } else {
            // Keep the \ for the parser (e.g. \{)
            value += advance(); 
            value += advance();
          }
        } else if (text[i] === quote) {
          break;
        } else { 
          value += advance(); 
        }
      }
      advance(); // skip closing quote
      tokens.push({ type: 'string', value, range: Range.create(startLine, startChar, line, char) });
      continue;
    }

    // numbers
    if (/[0-9]/.test(ch)) {
      const startLine = line;
      const startChar = char;
      let val = '';
      while (i < text.length && /[0-9.n]/.test(text[i])) val += advance();
      tokens.push({ type: 'number', value: val, range: Range.create(startLine, startChar, line, char) });
      continue;
    }

    // identifiers / keywords
    if (/[A-Za-z_]/.test(ch)) {
      const startLine = line;
      const startChar = char;
      let val = '';
      while (i < text.length && /[A-Za-z0-9_]/.test(text[i])) val += advance();
      const type = ALL_KEYWORDS.includes(val) ? 'keyword' : 'identifier';
      tokens.push({ type, value: val, range: Range.create(startLine, startChar, line, char) });
      continue;
    }

    // multi-char operators
    const multiCharOperators: { [key: string]: string } = {
      '==': 'equals_equals', '!=': 'not_equals', '<=': 'less_equals',
      '>=': 'greater_equals', '&&': 'and', '||': 'or', '=>': 'arrow', '|>': 'pipe',
      '...': 'dot_dot_dot'
    };
    let foundMulti = false;
    for (const op in multiCharOperators) {
      if (text.startsWith(op, i)) {
        const startLine = line;
        const startChar = char;
        for (let k = 0; k < op.length; k++) advance();
        tokens.push({ type: multiCharOperators[op], value: op, range: Range.create(startLine, startChar, line, char) });
        foundMulti = true;
        break;
      }
    }
    if (foundMulti) continue;

    // single-char tokens
    // single-char tokens
    const single: { [key: string]: string } = {
      '=': 'equals', '+': 'plus', '-': 'minus', '*': 'star', '/': 'slash',
      '.': 'dot', ',': 'comma', ':': 'colon', '(': 'lparen', ')': 'rparen',
      '[': 'lbracket', ']': 'rbracket', '{': 'lbrace', '}': 'rbrace',
      '!': 'not', '<': 'less', '>': 'greater', '|': 'vertical_bar', '&': 'ampersand'
    };
    const tt = single[ch];
    if (tt) {
      const startLine = line;
      const startChar = char;
      advance();
      tokens.push({ type: tt, value: ch, range: Range.create(startLine, startChar, line, char) });
      continue;
    }

    const startLine = line;
    const startChar = char;
    const val = advance();
    tokens.push({ type: 'illegal', value: val, range: Range.create(startLine, startChar, line, char) });
  }

  return tokens;
}

  function formatType(info: TypeInfo, depth: number = 0): string {
    if (!info) return 'unknown';
    if (info.unionTypes && info.unionTypes.length > 0) {
        return info.unionTypes.map(t => formatType(t, depth + 1)).join(' | ');
    }
    if (!info.kind) return info.name || 'unknown';
    if (info.kind === TypeKind.Unknown) return info.name || 'unknown';
    if (info.kind === TypeKind.Void) return 'void';

    if (info.kind === TypeKind.Function) {
        let pStr = '';
        if (info.params) {
            pStr = info.params.map((p, i) => {
               const pKind = info.paramTypes?.[i];
               let tStr = 'unknown';
               if (pKind) {
                   tStr = typeof pKind === 'string' ? pKind : formatType(pKind, depth + 1);
               }
               return `${p}: ${tStr}`;
            }).join(', ');
        }
        return `(${pStr}) => ${formatType(info.returnType || { kind: TypeKind.Void }, depth + 1)}`;
    }

    if (info.kind === TypeKind.Object && info.name && depth > 0 && !info.members) return info.name;

    if (info.kind === TypeKind.Array) {
        let res = 'Array';
        if (info.elementType) res += `<${formatType(info.elementType, depth + 1)}>`;
        else res += '<unknown>';
        return res;
    }

    let res = info.kind.toString();
    if (info.isClass && info.name && depth === 0) res = info.name;
    if (info.kind === TypeKind.Error) res = 'Error';
    
    const isBuiltinClass = info.isClass && (info.name === 'Array' || info.name === 'Error' || info.name === 'Map' || info.name === 'Date' || info.name === 'HashMap');

    if (info.members && info.members.size > 0 && depth < 3 && !isBuiltinClass) {
      if (res && res !== 'object' && res !== 'Array') res += ' ';
      res += '{\n';
      const props: string[] = [];
      const keys = Array.from(info.members.keys()).slice(0, 10);
      const indent = '  '.repeat(depth + 1);
      const closeIndent = '  '.repeat(depth);
      keys.forEach(k => {
        const v = info.members!.get(k);
        if (v && isNaN(Number(k))) {
          // Force 'this' to be a simple type name to avoid "double" class visualization
          if (k === 'this') {
            props.push(`${indent}${k}: ${v.name || 'Object'}`);
          } else {
            props.push(`${indent}${k}: ${formatType(v, depth + 1)}`);
          }
        }
      });
      res += props.join(',\n');
      if (info.members.size > 10) res += `,\n${indent}...`;
      res += `\n${closeIndent}}`;
    } else if (info.kind === TypeKind.Object && info.name) {
      return info.name;
    }
    return res;
  }

  function jsonToTypeInfo(obj: any): TypeInfo {
    if (typeof obj === 'number') return { kind: TypeKind.Number, value: String(obj) };
    if (typeof obj === 'string') return { kind: TypeKind.String, value: `"${obj}"` };
    if (typeof obj === 'boolean') return { kind: TypeKind.Boolean, value: String(obj) };
    if (Array.isArray(obj)) {
       const elType = obj.length > 0 ? jsonToTypeInfo(obj[0]) : { kind: TypeKind.Unknown };
       return { kind: TypeKind.Array, elementType: elType };
    }
    if (obj && typeof obj === 'object') {
       const members = new Map<string, SymbolInfo>();
       for (const key in obj) {
          members.set(key, { ...jsonToTypeInfo(obj[key]), name: key });
       }
       return { kind: TypeKind.Object, members, isStrict: true };
    }
    return { kind: TypeKind.Unknown };
  }

function isTypeCompatible(target: TypeInfo, source: TypeInfo): boolean {
  if (target.kind === TypeKind.Unknown || source.kind === TypeKind.Unknown) return true;
  
  if (target.unionTypes && target.unionTypes.length > 0) {
    return target.unionTypes.some(t => isTypeCompatible(t, source));
  }
  
  if (source.unionTypes && source.unionTypes.length > 0) {
    return source.unionTypes.every(s => isTypeCompatible(target, s));
  }

  if (target.kind === TypeKind.Array && source.kind === TypeKind.Array) {
    if (!target.elementType || target.elementType.kind === TypeKind.Unknown) return true;
    if (!source.elementType) return true; // source is empty array []
    return isTypeCompatible(target.elementType, source.elementType);
  }

  if (target.kind === source.kind) return true;

  // Standard conversions
  if (target.kind === TypeKind.Number && source.kind === TypeKind.BigInt) return true;
  if (target.kind === TypeKind.Object && (source.kind === TypeKind.Map || source.kind === TypeKind.Array || source.kind === TypeKind.Function || source.kind === TypeKind.Date || source.kind === TypeKind.Regex || source.kind === TypeKind.Error)) return true;
  
  // Nullability (simplified)
  if (source.kind === TypeKind.Null || source.kind === TypeKind.Undefined) return true;

  return false;
}

  function specializeType(type: TypeInfo): TypeInfo {
    if (type.receiver?.kind === TypeKind.Array && type.receiver.elementType && ARRAY_CALLBACK_METHODS.includes(type.name || '')) {
      const el = type.receiver.elementType;
      let retType = type.returnType;
      if (type.name === 'find') retType = el;
      if (type.name === 'filter') retType = { kind: TypeKind.Array, elementType: el };
      
      return {
          ...type,
          paramTypes: [
              { 
                  kind: TypeKind.Function, 
                  params: ['item'], 
                  paramTypes: [el], 
                  returnType: { kind: TypeKind.Unknown } 
              }
          ],
          returnType: retType
      };
    }
    return type;
  }

  function formatDoc(raw: string | undefined, type?: TypeInfo): string {
    if (raw && raw.startsWith('```sp')) {
       // Already has a header/formatted
       return raw;
    }

    let formatted = '';
    if (type) {
      const displayType = specializeType(type);
      if (displayType.params) {
        const paramStrings = displayType.params.map((name, i) => {
          const pSym = displayType.parameterSymbols?.[i];
          const pKind = displayType.paramTypes?.[i];
          const isRest = (displayType.hasRest || name.startsWith('...')) && i === (displayType.params!.length - 1);
          const cleanName = name.replace('...', '');
          let tStr = 'unknown';
          if (pSym) tStr = formatType(pSym);
          else if (pKind) {
              if (typeof pKind === 'string') tStr = pKind;
              else tStr = formatType(pKind);
          }
          return `${isRest ? '...' : ''}${cleanName}: ${tStr}`;
        });
        const rType = displayType.returnType ? formatType(displayType.returnType) : 'void';
        const typeHeader = displayType.isClass ? '(class)' : (displayType.params ? '(function)' : '(variable)');
        const nameHeader = displayType.name ? ` ${displayType.name}` : '';
        formatted += `\`\`\`sp\n${typeHeader}${nameHeader}(${paramStrings.join(', ')}): ${rType}\n\`\`\`\n\n`;
      } else {
        const typeStr = formatType(displayType);
        const prefix = displayType.isClass ? '(class)' : '(variable)';
        formatted += `\`\`\`sp\n${prefix} ${typeStr}\n\`\`\`\n\n`;
      }
    }
    
    if (!raw) return formatted.trim();

    // If it's already markdown but lacks the type header we just added, don't clean it
    if (raw.includes('**param**') || raw.includes('**Example**') || raw.includes('**Returns**')) {
      return (formatted + raw).trim();
    }

    // Clean up doc comment characters but preserve relative indentation and empty lines
    // Clean up doc comment characters but preserve relative indentation and empty lines
    const cleanedLines = raw.split(/\r?\n/).map(l => {
       if (l.trim().startsWith('*')) {
          const m = l.match(/^\s*\* ?(.*)$/);
          if (m) return m[1];
       }
       return l;
    }); 

    let inExample = false;
    for (let i = 0; i < cleanedLines.length; i++) {
      let line = cleanedLines[i];
      
      if (line.trim().startsWith('@param')) {
        if (inExample) { formatted += '```\n\n'; inExample = false; }
        const parts = line.trim().split(' ');
        if (parts.length >= 3) {
          const typeName = parts[1].replace(/[{}]/g, '');
          const paramName = parts[2];
          formatted += `**param** \`${paramName}\` — (${typeName}) ${parts.slice(3).join(' ')}\n\n`;
        }
      } else if (line.trim().startsWith('@return') || line.trim().startsWith('@returns')) {
        if (inExample) { formatted += '```\n\n'; inExample = false; }
        const parts = line.trim().split(' ');
        if (parts.length >= 2) {
          const typeName = parts[1].replace(/[{}]/g, '');
          formatted += `**Returns** (${typeName}) — ${parts.slice(2).join(' ')}\n\n`;
        }
      } else if (line.trim().startsWith('@example')) {
        if (inExample) { formatted += '```\n\n'; }
        formatted += `**Example**:\n\`\`\`sp\n`;
        const content = line.trim().substring(8).trim();
        if (content) formatted += content + '\n';
        inExample = true;
      } else {
        if (inExample) {
          formatted += line + '\n';
        } else {
          if (line.trim().length === 0) {
            formatted += '\n\n'; 
          } else {
            // Check if next line is empty or starts with @, if so finish current paragraph
            const nextLine = cleanedLines[i+1];
            if (nextLine === undefined || nextLine.trim().length === 0 || nextLine.trim().startsWith('@')) {
               formatted += line.trim() + '\n\n';
            } else {
               formatted += line.trim() + ' ';
            }
          }
        }
      }
    }
    if (inExample) formatted += '```\n';
    return formatted.trim();
  }

  function resolveExpressionType(expr: string, result: ParseResult, pos?: { line: number, character: number }): TypeInfo | undefined {
    if (!expr) return undefined;
    
    let target = expr.trim();
    let isAsync = false;
    if (target.startsWith('async ')) {
      isAsync = true;
      target = target.substring(6).trim();
    }

    // Depth-aware split by dots
    const partsRaw: string[] = [];
    let currentPart = '';
    let depth = 0;
    for (let i = 0; i < target.length; i++) {
        const c = target[i];
        if (c === '(') depth++;
        else if (c === ')') depth--;
        else if (c === '.' && depth === 0) {
            if (currentPart.length > 0) partsRaw.push(currentPart);
            currentPart = '';
            continue;
        }
        currentPart += c;
    }
    if (currentPart.length > 0) partsRaw.push(currentPart);

    if (partsRaw.length === 0) return undefined;

    let current: TypeInfo | undefined;
    const baseRaw = partsRaw[0];
    const base = baseRaw.includes('(') ? baseRaw.substring(0, baseRaw.indexOf('(')).trim() : baseRaw.trim();
    const baseIsCall = baseRaw.includes('(') && baseRaw.endsWith(')');

    // 1. Initial resolution (base)
    if (pos) {
        // Try exact match at the start of the expression
        const key = `${pos.line}:${pos.character - target.length}`;
        if (result.hoverMap.has(key)) {
            current = result.hoverMap.get(key);
        } else {
            // Try slightly to the left if after a dot
            const afterDotKey = `${pos.line}:${pos.character - target.length - 1}`;
            if (result.hoverMap.has(afterDotKey)) current = result.hoverMap.get(afterDotKey);
        }
    }

    if (!current) {
        // Check symbols first
        if (result.symbols.has(base)) {
          current = result.symbols.get(base);
        } else if (BUILTIN_MEMBERS.has(base)) {
          current = { kind: TypeKind.Object, members: BUILTIN_MEMBERS.get(base), name: base };
          if (base === 'Date') current.kind = TypeKind.Date;
          if (base === 'Map') current.kind = TypeKind.Map;
          if (base === 'regex') current.kind = TypeKind.Regex;
        }
    }

    if (current && baseIsCall && current.returnType) {
        current = current.returnType;
    }

    // 2. Chained resolution
    for (let i = 1; i < partsRaw.length && current; i++) {
        const memberRaw = partsRaw[i];
        const member = memberRaw.includes('(') ? memberRaw.substring(0, memberRaw.indexOf('(')).trim() : memberRaw.trim();
        const memberIsCall = memberRaw.includes('(') && memberRaw.endsWith(')');
        const receiver = current;

        const next = resolveMemberInfo(current, member);
        if (next) {
            current = { ...next, receiver, name: member };
            if (memberIsCall && current.returnType) {
                current = current.returnType;
            }
        } else {
            // Try fallback to symbols if it's a known global
            if (result.symbols.has(member)) {
                current = result.symbols.get(member);
            } else {
                current = undefined;
                break;
            }
        }
    }
    
    if (isAsync) {
       return { kind: TypeKind.Object, name: 'Future', members: BUILTIN_MEMBERS.get('Future'), documentation: 'Background execution future.' };
    }
    return current;
  }

  function getParsedResult(uri: string): ParseResult | undefined {
    if (!uri) return undefined;
    if (documentResults.has(uri)) return documentResults.get(uri);
    
    // Normalize and retry
    const norm = (u: string) => decodeURIComponent(u).replace(/\\/g, '/').toLowerCase();
    const target = norm(uri);
    
    // First try normalized direct match
    for (const [key, val] of documentResults.entries()) {
        if (norm(key) === target) return val;
    }

    // Then try fuzzy filename match as a last resort
    const filename = uri.split('/').pop() || '';
    if (filename) {
        for (const [key, val] of documentResults.entries()) {
            if (key.endsWith(filename)) return val;
        }
    }
    return undefined;
  }

  export function parseAndType(text: string, docUri: string): ParseResult {
  const tokens = tokenize(text);
  // Pass 1: Discovery and initial type propagation from call sites
  const pass1 = internalParseAndType(tokens, docUri);
  // Pass 2: Refine types using the knowledge gained in Pass 1
  const result = internalParseAndType(tokens, docUri, pass1.symbols);
  documentResults.set(docUri, result);
  return result;
}

function internalParseAndType(tokens: Token[], filePath: string, initialSymbols?: SymbolTable): ParseResult {
  const isSpd = filePath.endsWith('.spd');
  const diagnostics: Diagnostic[] = [];
  
  tokens.forEach(t => {
    if (t.type === 'illegal') {
      diagnostics.push({
        range: t.range,
        message: `Syntax error: Unsupported symbol '${t.value}'`,
        severity: DiagnosticSeverity.Error
      });
    }
  });

  const symbols: SymbolTable = initialSymbols || new Map();
  const importedModules: string[] = [];
  let lastDoc: string | undefined;

  let currentScope: Map<string, SymbolInfo> = symbols;

  // Global built-ins (ensure they exist)
  if (!symbols.has('time')) symbols.set('time', { kind: TypeKind.Number });
  if (!symbols.has('floor')) symbols.set('floor', { kind: TypeKind.Number, params: ['x'], paramTypes: [TypeKind.Number], returnType: { kind: TypeKind.Number } });
  if (!symbols.has('range')) {
    symbols.set('range', { 
      kind: TypeKind.Array, 
      params: ['start', 'end'], 
      paramTypes: [TypeKind.Number, TypeKind.Number], 
      returnType: { kind: TypeKind.Array, elementType: { kind: TypeKind.Number } },
      documentation: 'Returns an array of numbers from start (inclusive) to end (exclusive).'
    });
  }
  if (!symbols.has('regex')) {
    symbols.set('regex', {
      kind: TypeKind.Regex,
      name: 'regex_builder',
      params: ['pattern'],
      paramTypes: [TypeKind.String],
      returnType: { kind: TypeKind.Regex, name: 'regex_final' },
      documentation: 'Creates a new Regex object or builder.'
    });
  }
  if (!symbols.has('net')) symbols.set('net', { kind: TypeKind.Object, members: BUILTIN_MEMBERS.get('net'), name: 'net' });
  if (!symbols.has('http')) symbols.set('http', { kind: TypeKind.Object, members: BUILTIN_MEMBERS.get('http'), name: 'http' });
  if (!symbols.has('console')) symbols.set('console', { kind: TypeKind.Object, members: BUILTIN_MEMBERS.get('console'), name: 'console' });
  if (!symbols.has('every')) {
    symbols.set('every', { 
      kind: TypeKind.Function, 
      params: ['ms', 'callback'], 
      paramTypes: [TypeKind.Number, TypeKind.Function], 
      returnType: { kind: TypeKind.Object, name: 'Timer', members: BUILTIN_MEMBERS.get('Timer') },
      documentation: 'Schedules a repeating task every X milliseconds.\n\nExample:\nevery 1000 {\n  console.show("TICK")\n}' 
    });
  }
  if (!symbols.has('after')) {
    symbols.set('after', { 
      kind: TypeKind.Function, 
      params: ['ms', 'callback'], 
      paramTypes: [TypeKind.Number, TypeKind.Function], 
      returnType: { kind: TypeKind.Object, name: 'Timer', members: BUILTIN_MEMBERS.get('Timer') },
      documentation: 'Schedules a one-time task after X milliseconds.\n\nExample:\nafter 5000 {\n  console.show("BOOM")\n}' 
    });
  }
  if (!symbols.has('async')) {
    symbols.set('async', {
      kind: TypeKind.Function,
      params: ['callback'],
      paramTypes: [TypeKind.Function],
      returnType: { kind: TypeKind.Object, name: 'Future', members: BUILTIN_MEMBERS.get('Future') },
      documentation: 'Runs an expression or block asynchronously, returning a Future.\n\nExample:\nasync {\n  return net.get("https://google.com")\n}'
    });
  }

  const resolveModule = (modName: string): SymbolTable | undefined => {
    try {
      const fsPath = decodeURIComponent(filePath.replace(/^file:\/\/\//, '/').replace(/^file:\/\//, ''));
      const fullDir = path.dirname(fsPath);
      const candidates = [modName, modName + '.sp', modName + '.spd', path.join('modules', modName), path.join('modules', modName + '.sp'), path.join('modules', modName + '.spd')];
      
      for (const alias of candidates) {
        const targetPath = path.resolve(fullDir, alias);
        if (fs.existsSync(targetPath)) {
          const targetUri = 'file://' + targetPath;
          if (documentResults.has(targetUri)) return documentResults.get(targetUri)!.symbols;
          const content = fs.readFileSync(targetPath, 'utf8');
          const res = parseAndType(content, targetUri);
          return res.symbols;
        }
      }
    } catch (e) { /* silent */ }
    if (modName.startsWith('native_')) return new Map();
    return undefined;
  };
  if (!symbols.has('exit')) {
    symbols.set('exit', {
      kind: TypeKind.Void,
      params: ['code'],
      paramTypes: [TypeKind.Number],
      documentation: 'Terminates the process with an optional exit code.'
    });
  }
  if (!symbols.has('Error')) {
    symbols.set('Error', { 
      kind: TypeKind.Function, 
      params: ['message', 'line'],
      paramTypes: [TypeKind.String, TypeKind.Number],
      returnType: { kind: TypeKind.Error, members: BUILTIN_MEMBERS.get('Error'), name: 'Error' }, 
      documentation: 'Functional error constructor.' 
    });
  }
  if (!symbols.has('Date')) symbols.set('Date', { kind: TypeKind.Date, members: BUILTIN_MEMBERS.get('Date'), name: 'Date', documentation: 'Date and time operations.' });
  if (!symbols.has('Map')) symbols.set('Map', { kind: TypeKind.Map, members: BUILTIN_MEMBERS.get('Map'), name: 'Map', documentation: 'Standard Map data structure.' });
  if (!symbols.has('HashMap')) symbols.set('HashMap', { kind: TypeKind.Map, members: BUILTIN_MEMBERS.get('Map'), name: 'HashMap', documentation: 'Standard HashMap data structure.' });

  let index = 0;
  const hoverMap: Map<string, TypeInfo> = new Map();
  const functionStack: SymbolInfo[] = [];

  function addHover(range: Range, type: TypeInfo) {
    hoverMap.set(`${range.start.line}:${range.start.character}`, type);
  }

  function peek(): Token | undefined { return index < tokens.length ? tokens[index] : undefined; }
  function consume(): Token | undefined { const t = peek(); if (t) index++; return t; }
  function expect(type: string): Token | undefined {
    const t = peek();
    if (!t || t.type !== type) {
      if (t) diagnostics.push({ severity: DiagnosticSeverity.Error, range: t.range, message: `Expected ${type} but found ${t.type}`, source: 'sp' });
      return undefined;
    }
    return consume();
  }

  function parseTypeAnnotation(): TypeInfo {
    if (peek()?.type === 'colon') {
      const colonTok = consume()!; // :
      const result = parseTypeAnnotationTypeOnly();
      if (!result.range) {
          result.range = { start: colonTok.range.start, end: tokens[index-1]?.range.end || colonTok.range.end };
      }
      return result;
    }
    return { kind: TypeKind.Unknown };
  }

  function parseTypeAnnotationTypeOnly(): TypeInfo {
    const t = peek();
    if (!t) return { kind: TypeKind.Unknown };

    let result: TypeInfo;

    if (t.type === 'lparen') {
      consume(); // (
      const params: string[] = [];
      const paramTypes: TypeInfo[] = [];
      while (peek() && peek()?.type !== 'rparen') {
        const pTok = expect('identifier');
        if (pTok && peek()?.type === 'colon') {
           const pType = parseTypeAnnotation();
           params.push(pTok.value);
           paramTypes.push(pType);
        } else if (pTok) {
           params.push(pTok.value);
           paramTypes.push({ kind: TypeKind.Unknown });
        }
        if (peek()?.type === 'comma') consume(); else break;
      }
      expect('rparen');
      let returnType: TypeInfo = { kind: TypeKind.Unknown };
      if (peek()?.type === 'arrow') {
        consume(); // =>
        returnType = parseTypeAnnotationTypeOnly();
      }
      result = { kind: TypeKind.Function, params, paramTypes, returnType };
    } else if (t.type === 'lbrace') {
      consume(); // {
      const members = new Map<string, TypeInfo>();
      while (peek() && peek()?.type !== 'rbrace') {
        const pTok = expect('identifier');
        if (pTok) {
          expect('colon');
          members.set(pTok.value, parseTypeAnnotationTypeOnly());
          if (peek()?.type === 'comma') consume();
        } else break;
      }
      expect('rbrace');
      result = { kind: TypeKind.Object, members };
    } else {
      const startTok = consume()!;
      let typeFull = startTok.value;
      let endTok = startTok;
      while (peek()?.type === 'dot') {
        consume();
        const next = expect('identifier');
        if (next) {
          typeFull += '.' + next.value;
          endTok = next;
        } else break;
      }
      const typeRange = { start: startTok.range.start, end: endTok.range.end };
      
      const kind = stringToTypeKind(typeFull);
      
      let known: TypeInfo | undefined = currentScope.get(typeFull);
      if (!known && typeFull.includes('.')) {
        const parts = typeFull.split('.');
        let curr: TypeInfo | undefined = currentScope.get(parts[0]);
        for (let i = 1; i < parts.length && curr; i++) {
          curr = resolveMemberInfo(curr, parts[i]) || undefined;
        }
        known = curr;
      }
      
      if (!known) {
        // Fallback: search across all imported modules for the type name
        for (const [key, sym] of currentScope.entries()) {
           if (sym.kind === TypeKind.Object && sym.members?.has(typeFull)) {
              known = sym.members.get(typeFull);
              break;
           }
        }
      }

      if (known) {
        result = { ...known };
      } else {
        result = { kind, name: typeFull };
      }
      addHover(typeRange, result);

      if (peek()?.type === 'less') {
        consume(); // <
        const args: TypeInfo[] = [];
        while (peek() && peek()?.type !== 'greater') {
          args.push(parseTypeAnnotationTypeOnly());
          if (peek()?.type === 'comma') consume(); else if (peek()?.type !== 'greater') break;
        }
        expect('greater');
        if (args.length > 0) result.elementType = args[0];
      }
    }

    while (peek()?.type === 'vertical_bar' || peek()?.type === 'ampersand') {
      const op = consume()!.type;
      const other = parseTypeAnnotationTypeOnly();
      if (op === 'vertical_bar') {
        if (!result.unionTypes) result.unionTypes = [ { ...result } ];
        result.unionTypes.push(other);
        result.kind = TypeKind.Unknown;
      } else {
        // Intersection
        if (!result.intersectionTypes) result.intersectionTypes = [ { ...result } ];
        result.intersectionTypes.push(other);
        
        // Merge members for easier hover
        if (result.kind === TypeKind.Object && (other.kind === TypeKind.Object || other.members)) {
          const merged = new Map(result.members || []);
          if (other.members) {
            for (const [k, v] of other.members) merged.set(k, v);
          }
          result.members = merged;
        }
      }
    }

    return result;
  }

  function parseExpression(depth = 0, expected?: TypeInfo): TypeInfo { 
    if (depth > 50) return { kind: TypeKind.Unknown };
    return parsePipe(depth, expected);
  }

  function parsePipe(depth: number, expected?: TypeInfo): TypeInfo {
    let left = parseLogicOr(depth, expected);
    while (peek()?.type === 'pipe') {
      consume(); // |>
      while (peek()?.type === 'vertical_bar') { consume(); consume(); }
      const prevUnderscore = currentScope.get('_');
      currentScope.set('_', { ...left, isMutable: false });
      // RHS often expects _ to be a member or start of expression
      left = parseExpression(depth + 1);
      if (prevUnderscore) currentScope.set('_', prevUnderscore);
      else currentScope.delete('_');
    }
    return left;
  }

  function parseLogicOr(depth: number, expected?: TypeInfo): TypeInfo {
    let left = parseLogicAnd(depth, expected);
    while (peek()?.type === 'or') {
      consume();
      parseLogicAnd(depth, expected);
      left = { kind: TypeKind.Boolean, range: left.range };
    }
    return left;
  }

  function parseLogicAnd(depth: number, expected?: TypeInfo): TypeInfo {
    let left = parseEquality(depth, expected);
    while (peek()?.type === 'and') {
      consume();
      parseEquality(depth, expected);
      left = { kind: TypeKind.Boolean, range: left.range };
    }
    return left;
  }

  function parseEquality(depth: number, expected?: TypeInfo): TypeInfo {
    let left = parseComparison(depth, expected);
    while (peek() && (peek()!.type === 'equals_equals' || peek()!.type === 'not_equals')) {
      consume();
      parseComparison(depth + 1, expected);
      left = { kind: TypeKind.Boolean, range: left.range };
    }
    return left;
  }

  function parseComparison(depth: number, expected?: TypeInfo): TypeInfo {
    let left = parseAdditive(depth, expected);
    const t = peek()?.type;
    if (t === 'less' || t === 'greater' || t === 'less_equals' || t === 'greater_equals') {
      consume();
      parseAdditive(depth, expected);
      left = { kind: TypeKind.Boolean, range: left.range };
    }
    return left;
  }

  function parseAdditive(depth: number, expected?: TypeInfo): TypeInfo {
    let left = parseMultiplicative(depth, expected);
    while (peek() && (peek()!.type === 'plus' || peek()!.type === 'minus')) {
      const op = consume()!;
      const right = parseMultiplicative(depth + 1, expected);
      
      if (left.kind === TypeKind.String || right.kind === TypeKind.String) {
        left = { kind: TypeKind.String, range: { start: left.range?.start || op.range.start, end: right.range?.end || op.range.end } };
      } else if (left.kind === TypeKind.BigInt || right.kind === TypeKind.BigInt) {
        left = { kind: TypeKind.BigInt };
      } else {
        left = { kind: TypeKind.Number };
      }
    }
    return left;
  }

  function parseMultiplicative(depth: number, expected?: TypeInfo): TypeInfo {
    let left = parseUnary(depth, expected);
    while (peek()?.type === 'star' || peek()?.type === 'slash' || peek()?.type === 'percent') {
      const op = consume()!;
      let right = parseUnary(depth + 1, expected);

      if (left.kind === TypeKind.Unknown && left.name) {
        const sym = currentScope.get(left.name);
        if (sym) sym.kind = TypeKind.Number;
      }
      if (right.kind === TypeKind.Unknown && right.name) {
        const sym = currentScope.get(right.name);
        if (sym) sym.kind = TypeKind.Number;
      }

      if (left.kind === TypeKind.BigInt || right.kind === TypeKind.BigInt) {
        left = { kind: TypeKind.BigInt, range: { start: left.range?.start || op.range.start, end: right.range?.end || op.range.end } };
      } else {
        left = { kind: TypeKind.Number, range: { start: left.range?.start || op.range.start, end: right.range?.end || op.range.end } };
      }
    }
    return left;
  }

  function parseUnary(depth: number, expected?: TypeInfo): TypeInfo {
    const t = peek()?.type;
    if (t === 'not' || t === 'minus') {
      const op = consume()!;
      const right = parseUnary(depth + 1);
      return { ...right, range: { start: op.range.start, end: right.range?.end || op.range.end } };
    }
    if (peek()?.type === 'keyword' && peek()?.value === 'typeof') {
      const op = consume()!;
      const right = parseUnary(depth + 1);
      return { kind: TypeKind.String, range: { start: op.range.start, end: right.range?.end || op.range.end } };
    }
    return parseAs(depth, expected);
  }

  function parseAs(depth: number, expected?: TypeInfo): TypeInfo {
    let left = parsePostFix(depth, expected);
    if (peek()?.type === 'keyword' && peek()?.value === 'as') {
      consume(); // as
      const typeResult = parseTypeAnnotationTypeOnly();
      const oldRange = left.range;
      left = { ...typeResult, range: { start: oldRange?.start || typeResult.range?.start || { line: 0, character: 0 }, end: typeResult.range?.end || { line: 0, character: 0 } } };
    }
    return left;
  }

  function parsePostFix(depth: number, expected?: TypeInfo): TypeInfo {
    let left = parsePrimary(depth, expected);
    while (true) {
      const next = peek();
      if (!next) break;

      if (next.type === 'dot') {
        const dotRange = next.range;
        consume();
        if (peek()?.type === 'dot') {
           diagnostics.push({
              range: { start: dotRange.start, end: peek()!.range.end },
              message: "Unexpected consecutive dots '..'. Did you mean '.'?",
              severity: DiagnosticSeverity.Error
           });
           while(peek()?.type === 'dot') consume();
        }
        const propTok = peek();
        if (propTok && (propTok.type === 'identifier' || propTok.type === 'keyword')) {
          consume();
          const propName = propTok.value;
          let propType = resolveMemberInfo(left, propName);
          const isStrict = left.isStrict || (left.name && BUILTIN_MEMBERS.has(left.name));
          
          if (!propType) {
             const typeName = left.name || left.kind;
             if (isStrict) {
                tentativeDiagnostics.push({
                   range: propTok.range,
                   message: `Property '${propName}' does not exist on type '${typeName}'.`,
                   severity: DiagnosticSeverity.Error
                });
                propType = { kind: TypeKind.Unknown, name: propName };
             } else if (!left.isNative) {
                // Usage-based inference for non-strict objects
                const localMembers = left.members ? new Map(left.members) : new Map<string, SymbolInfo>();
                if (!localMembers.has(propName)) {
                   localMembers.set(propName, { kind: TypeKind.Unknown, name: propName });
                   left = { ...left, members: localMembers };
                }
                propType = localMembers.get(propName)!;
             } else {
                propType = { kind: TypeKind.Unknown, name: propName };
             }
          }
          const oldRange = left.range;
          const receiver = left;
          left = { ...propType, range: { start: oldRange?.start || propTok.range.start, end: propTok.range.end } };
          left.receiver = receiver;
          left.name = propName;
          addHover(propTok.range, left);
        }
      } else if (next.type === 'lbracket') {
        consume(); // [
        parseExpression(depth + 1);
        const rPos = expect('rbracket');
        if (left.kind === TypeKind.Array && left.elementType) {
          const oldRange = left.range;
          left = { ...left.elementType, range: { start: oldRange?.start || next.range.start, end: rPos?.range.end || next.range.end } };
        } else {
          left = { kind: TypeKind.Unknown, range: { start: left.range?.start || next.range.start, end: rPos?.range.end || next.range.end } };
        }
      } else if (next.type === 'lparen') {
        const argTokens: Token[] = [];
        consume(); // (
        const argTypes: TypeInfo[] = [];
        if (peek() && peek()!.type !== 'rparen') {
          let paramIdx = 0;
          do {
            const t = peek();
            if (t?.type === 'dot_dot_dot') consume();
            if (t) argTokens.push(t);
            
            let argExpected: TypeInfo | undefined;
            if (left.receiver?.kind === TypeKind.Array && ARRAY_CALLBACK_METHODS.includes(left.name || '') && paramIdx === 0) {
                // Callback inference: expected function with paramTypes[0] = elementType
                const elType = left.receiver.elementType || { kind: TypeKind.Unknown };
                argExpected = { 
                    kind: TypeKind.Function, 
                    params: ['item', 'index', 'array'],
                    paramTypes: [elType, { kind: TypeKind.Number }, { kind: TypeKind.Array, elementType: elType }],
                    returnType: left.name === 'map' ? { kind: TypeKind.Unknown } : { kind: TypeKind.Boolean }
                };
                if (left.name === 'forEach') argExpected.returnType = { kind: TypeKind.Void };
            }

            argTypes.push(parseExpression(depth + 1, argExpected));
            paramIdx++;
          } while (peek()?.type === 'comma' && consume() !== null);
        }
        const rPos = expect('rparen');
        
        // Argument count validation
        if (left.params) {
            const actualCount = argTypes.length;
            const hasRest = left.hasRest || (left.params.length > 0 && left.params[left.params.length - 1].startsWith('...'));
            
            let minExpected = 0;
            let maxExpected = left.params.length;
            
            for (const p of left.params) {
                if (!p.endsWith('?') && !p.startsWith('...')) minExpected++;
            }

            if (hasRest) {
                if (actualCount < minExpected) {
                    diagnostics.push({
                        range: { start: next.range.start, end: rPos?.range.end || next.range.end },
                        severity: DiagnosticSeverity.Warning,
                        message: `Expected at least ${minExpected} arguments, but got ${actualCount}.`,
                        source: 'sp-language'
                    });
                }
            } else if (actualCount < minExpected || actualCount > maxExpected) {
                const rangeStr = minExpected === maxExpected ? `${minExpected}` : `${minExpected}-${maxExpected}`;
                diagnostics.push({
                    range: { start: next.range.start, end: rPos?.range.end || next.range.end },
                    severity: DiagnosticSeverity.Warning,
                    message: `Expected ${rangeStr} arguments, but got ${actualCount}.`,
                    source: 'sp-language'
                });
            }
            
            // Argument type validation
            if (left.paramTypes && left.paramTypes.length > 0) {
                for (let i = 0; i < Math.min(argTypes.length, left.paramTypes.length); i++) {
                    const argType = argTypes[i];
                    const expectedRaw = left.paramTypes[i];
                    const expectedType: TypeInfo = (typeof expectedRaw === 'string') ? { kind: expectedRaw as TypeKind } : expectedRaw;
                    if (!isTypeCompatible(expectedType, argType)) {
                        const expectedStr = formatType(expectedType);
                        const actualStr = formatType(argType);
                        diagnostics.push({
                            range: argType.range || { start: next.range.start, end: rPos?.range.end || next.range.end },
                            severity: DiagnosticSeverity.Warning,
                            message: `Argument of type '${actualStr}' is not assignable to parameter of type '${expectedStr}'.`,
                            source: 'sp-language',
                            data: {
                                expectedType: expectedStr,
                                foundType: actualStr,
                                valueRange: argType.range
                            }
                        });
                    }
                }
            }
        }

        if (left.name === 'readJson' && argTypes.length > 0) {
           let jsonName = '';
           if (argTypes[0].kind === TypeKind.String && argTypes[0].value) {
              jsonName = argTypes[0].value.replace(/['"]/g, '');
           } else if (argTypes[0].name) {
              const sym = currentScope.get(argTypes[0].name);
              if (sym?.value && typeof sym.value === 'string') {
                 jsonName = sym.value.replace(/['"]/g, '');
              }
           }
           if (jsonName) {
              try {
                  const fsPath = decodeURIComponent(filePath.replace(/^file:\/\/\//, '/').replace(/^file:\/\//, ''));
                  const fullDir = path.dirname(fsPath);
                  const fullPath = path.resolve(fullDir, jsonName);
                  if (fs.existsSync(fullPath)) {
                     const content = fs.readFileSync(fullPath, 'utf8');
                     const parsed = JSON.parse(content);
                     left.returnType = jsonToTypeInfo(parsed);
                  }
              } catch (e: any) { /* silent */ }
           }
        }

        let res: TypeInfo = left.returnType || { kind: TypeKind.Unknown };
        
        if (left.isClass) {
          res = { ...left, isClass: false };
        }
        
        // Special case for Array built-ins return types
        if (left.receiver?.kind === TypeKind.Array) {
            if (left.name === 'find') {
                res = left.receiver.elementType || { kind: TypeKind.Unknown };
            } else if (left.name === 'filter') {
                res = { kind: TypeKind.Array, elementType: left.receiver.elementType || { kind: TypeKind.Unknown } };
            } else if (left.name === 'map' && argTypes.length > 0) {
                res = { kind: TypeKind.Array, elementType: argTypes[0].returnType || { kind: TypeKind.Unknown } };
            }
        }

        // Propagate types back to parameter symbols if known
        if (left.parameterSymbols) {
          for (let i = 0; i < left.parameterSymbols.length; i++) {
            const pSym = left.parameterSymbols[i];
            if (i < argTypes.length) {
              if (i === left.parameterSymbols.length - 1 && left.hasRest) {
                 pSym.kind = TypeKind.Array;
              } else if (pSym.kind === TypeKind.Unknown) {
                pSym.kind = argTypes[i].kind;
                if (argTypes[i].members) pSym.members = new Map(argTypes[i].members);
              }
            }
          }
        }
        const oldRange = left.range;
        left = { ...res, range: { start: oldRange?.start || next.range.start, end: rPos?.range.end || next.range.end } };
      } else if (next.type === 'lbracket') {
        consume();
        if (peek() && peek()!.type !== 'rbracket') {
          do {
            if (peek()?.type === 'dot_dot_dot') consume();
            parseExpression(depth + 1);
          } while (peek()?.type === 'comma' && consume() !== null);
        }
        const rPos = expect('rbracket');
        left = { kind: TypeKind.Unknown, range: { start: left.range?.start || next.range.start, end: rPos?.range.end || next.range.end } };
      } else {
        break;
      }
    }
    return left;
  }

  interface DestructInfo {
    name: string;
    propName?: string;
    nested?: DestructInfo[];
    isRest?: boolean;
    isObject?: boolean;
    range?: Range;
    propRange?: Range;
  }

  function parsePrimary(depth: number, expected?: TypeInfo): TypeInfo {
    const startTok = peek();
    if (!startTok) return { kind: TypeKind.Unknown };

    if (startTok.type === 'number') { 
      consume(); 
      if (startTok.value.endsWith('n')) return { kind: TypeKind.BigInt, value: startTok.value, range: startTok.range };
      return { kind: TypeKind.Number, value: startTok.value, range: startTok.range }; 
    }
    if (startTok.type === 'string') { 
        const tok = consume()!;
        const val = tok.value;
        const interpolations = val.matchAll(/\{([^{}]+)\}/g);
        for (const match of interpolations) {
            const expr = match[1];
            if (match.index === undefined) continue;
            // Precise character offset within the document
            const startChar = tok.range.start.character + match.index + 1;
            const exprTokens = tokenize(expr);
            // Temporary offset adjustment for sub-tokens so hovers map to the right place
            exprTokens.forEach(t => {
                t.range.start.line = tok.range.start.line;
                t.range.end.line = tok.range.start.line;
                t.range.start.character += startChar;
                t.range.end.character += startChar;
            });
            
            // Sub-parse the interpolation content in the CURRENT scope
            const oldIndex = index;
            const oldTokens = tokens;
            tokens = exprTokens;
            index = 0;
            try {
                // If expression is "this.name", it will lookup 'this' in classMembers correctly
                parseExpression(0);
            } catch (e) { /* ignore interpolation errors */ }
            tokens = oldTokens;
            index = oldIndex;
        }
        return { kind: TypeKind.String, value: val, range: tok.range }; 
    }
    if (startTok.type === 'keyword' && (startTok.value === 'true' || startTok.value === 'false')) { consume(); return { kind: TypeKind.Boolean, range: startTok.range }; }
    if (startTok.type === 'keyword' && startTok.value === 'null') { consume(); return { kind: TypeKind.Null, range: startTok.range }; }
    if (startTok.type === 'keyword' && startTok.value === 'undefined') { consume(); return { kind: TypeKind.Undefined, range: startTok.range }; }
    if (startTok.type === 'keyword' && startTok.value === 'this') {
      consume();
      const res = currentScope.get('this') || { kind: TypeKind.Object };
      addHover(startTok.range, res);
      return { ...res, range: startTok.range };
    }

    if (startTok.type === 'lbracket') {
      consume(); // [
      const members = new Map<string, TypeInfo>();
      const elements: TypeInfo[] = [];
      let idx = 0;
      
      const expectedEl = (expected?.kind === TypeKind.Array) ? expected.elementType : undefined;

      while (peek() && peek()!.type !== 'rbracket') {
        const itemTok = peek()!;
        if (peek()?.type === 'dot_dot_dot') {
          consume();
          const spread = parseExpression(depth + 1, expected); // Expecting same array type or element? Spread is tricky
          if (spread.members) spread.members.forEach((v, k) => {
             if (!isNaN(Number(k))) elements.push(v);
             members.set(k, v);
          });
        } else {
          const type = parseExpression(depth + 1, expectedEl);
          if (expectedEl && !isTypeCompatible(expectedEl, type)) {
              diagnostics.push({
                range: type.range || itemTok.range,
                message: `Type mismatch in array element: expected ${formatType(expectedEl)}, but found ${formatType(type)}`,
                severity: DiagnosticSeverity.Error,
                data: {
                    expectedType: formatType(expectedEl),
                    foundType: formatType(type),
                    valueRange: type.range || itemTok.range
                }
              });
          }
          elements.push(type);
          members.set(idx.toString(), type);
          idx++;
        }
        if (peek()?.type === 'comma') consume(); else break;
      }
      const rPos = expect('rbracket');
      let elementType: TypeInfo = expectedEl || { kind: TypeKind.Unknown };
      if (!expectedEl && elements.length > 0) {
        elementType = elements[0];
        for (let i = 1; i < elements.length; i++) {
          elementType = mergeTypes(elementType, elements[i]);
        }
      }

      return { 
        kind: TypeKind.Array, 
        members: new Map([['length', { kind: TypeKind.Number }], ...members]),
        elementType: elementType,
        range: { start: startTok.range.start, end: rPos?.range.end || startTok.range.end }
      };
    }

    if (startTok.type === 'identifier' || (startTok.type === 'keyword' && BUILTIN_OBJECTS.includes(startTok.value))) {
      const name = startTok.value;
      const nameTok = consume()!;
      let res = currentScope.get(name) || symbols.get(name);
      
      if (!res && BUILTIN_MEMBERS.has(name)) {
          res = { kind: TypeKind.Object, members: BUILTIN_MEMBERS.get(name), name: name };
          if (name === 'regex') res.kind = TypeKind.Regex;
      }
      
      if (!res) {
          res = { kind: TypeKind.Unknown, name: name };
          diagnostics.push({
            range: startTok.range,
            message: `Undefined identifier: '${name}'`,
            severity: DiagnosticSeverity.Error
          });
      }

      addHover(nameTok.range, res);
      if (!res.name || (res.kind === TypeKind.Unknown && res.name === name)) {
          res.name = name;
      }
      return { ...res, range: startTok.range };
    }

    if (startTok.type === 'keyword' && startTok.value === 'async') {
      consume(); // async
      let bodyType: TypeInfo = { kind: TypeKind.Unknown };
      if (peek()?.type === 'lbrace') {
        const prevScope = currentScope;
        currentScope = new Map(currentScope);
        bodyType = parseBlock();
        currentScope = prevScope;
      } else {
        bodyType = parseExpression(depth + 1);
      }
      return { kind: TypeKind.Object, name: 'Future', members: BUILTIN_MEMBERS.get('Future'), returnType: bodyType };
    }

    if (startTok.type === 'keyword' && (startTok.value === 'after' || startTok.value === 'every')) {
      consume(); // after or every
      parseExpression(depth + 1); // delay
      let bodyType: TypeInfo = { kind: TypeKind.Unknown };
      if (peek()?.type === 'lbrace') {
        const prevScope = currentScope;
        currentScope = new Map(currentScope);
        bodyType = parseBlock();
        currentScope = prevScope;
      } else {
        bodyType = parseExpression(depth + 1);
      }
      return { kind: TypeKind.Object, name: 'Timer', members: BUILTIN_MEMBERS.get('Timer'), returnType: bodyType };
    }

    if (startTok.type === 'lparen') {
      consume(); // consume (
      
      const isIdentifierLike = (t: Token | undefined) => t && (t.type === 'identifier' || (t.type === 'keyword' && BUILTIN_OBJECTS.includes(t.value)));

      // Look ahead to see if it's a lambda or grouped expression
      let isLambda = false;
      const nextTok = peek();
      if (nextTok?.type === 'rparen') {
          if (tokens[index + 1]?.type === 'arrow') isLambda = true;
      } else if (nextTok?.type === 'dot_dot_dot') {
          isLambda = true;
      } else if (isIdentifierLike(nextTok)) {
          let searchIdx = index;
          let depth = 1;
          while (searchIdx < tokens.length && depth > 0) {
            const t = tokens[searchIdx++];
            if (t.type === 'lparen') depth++;
            else if (t.type === 'rparen') depth--;
            else if (depth === 1 && (t.type === 'colon' || t.type === 'comma' || t.type === 'dot_dot_dot')) {
              isLambda = true;
              break;
            }
          }
          if (!isLambda && depth === 0 && tokens[searchIdx]?.type === 'arrow') isLambda = true;
      }
      if (isLambda) {
          let params: string[] = [];
          let parameterSymbols: SymbolInfo[] = [];
          let paramTypes: TypeInfo[] = [];
          let hasRest = false;
          
          const prevScope = currentScope;
          currentScope = new Map(currentScope); // New scope for lambda body (if we were evaluating, but helpful for nested hover)

          if (peek() && peek()!.type !== 'rparen') {
              let pendingParams: SymbolInfo[] = [];
              let paramIdx = 0;
              do {
                  if (peek()?.type === 'dot_dot_dot') { consume(); hasRest = true; }
                  const paramTok = expect('identifier') || (peek()?.type === 'keyword' && BUILTIN_OBJECTS.includes(peek()!.value) ? consume() : undefined);
                  if (paramTok) {
                      const sym: SymbolInfo = { kind: TypeKind.Unknown, name: paramTok.value, isMutable: true };
                      
                      // Contextual inference from expectedType
                      if (expected?.paramTypes?.[paramIdx]) {
                          const et = expected.paramTypes[paramIdx];
                          if (typeof et === 'string') {
                              sym.kind = et;
                          } else {
                              Object.assign(sym, et);
                          }
                      }

                      params.push(paramTok.value);
                      parameterSymbols.push(sym);
                      paramTypes.push(sym);
                      pendingParams.push(sym);
                      currentScope.set(paramTok.value, sym);
                      addHover(paramTok.range, sym);
                      
                      if (peek()?.type === 'colon') {
                          consume(); // :
                          const result = parseTypeAnnotationTypeOnly();
                          pendingParams.forEach(p => {
                            const { name: _typeName, range: _typeRange, ...typeProps } = result;
                            Object.assign(p, typeProps);
                          });
                          pendingParams = [];
                      }
                      paramIdx++;
                  }
              } while (peek()?.type === 'comma' && consume());
          }
          expect('rparen');
          expect('arrow');

          const lambdaType: SymbolInfo = { 
            kind: TypeKind.Function, 
            params, 
            parameterSymbols,
            paramTypes,
            hasRest, 
            returnType: { kind: TypeKind.Unknown },
            name: '(lambda)'
          };

          functionStack.push(lambdaType);
          const bodyType = peek()?.type === 'lbrace' ? parseBlock() : parseExpression(depth + 1);
          functionStack.pop();
          lambdaType.returnType = bodyType;

          currentScope = prevScope;
          return lambdaType;
      } else {
          const inner = parseExpression(depth + 1);
          expect('rparen');
          return inner;
      }
    }

    if (startTok.type === 'lbrace') {
      consume();
      const members = new Map<string, TypeInfo>();
      while (peek() && peek()!.type !== 'rbrace') {
        if (peek()?.type === 'dot_dot_dot') {
          consume();
          const spread = parseExpression(depth + 1);
          if (spread.members) spread.members.forEach((v, k) => members.set(k, v));
        } else {
          const propTok = consume();
          if (propTok && (propTok.type === 'identifier' || propTok.type === 'keyword')) {
            expect('colon');
            const valType = parseExpression(depth + 1);
            members.set(propTok.value, valType);
            addHover(propTok.range, valType);
          }
        }
        if (peek()?.type === 'comma') consume(); else break;
      }
      expect('rbrace');
      return { kind: TypeKind.Object, members };
    }

    if (startTok.type === 'keyword' && startTok.value === 'match') {
      consume();
      // Optional parentheses
      let hasParens = false;
      if (peek()?.type === 'lparen') {
        consume();
        hasParens = true;
      }
      parseExpression(depth + 1);
      if (hasParens) expect('rparen');

      expect('lbrace');
      const armTypes: TypeInfo[] = [];
      while (peek() && peek()!.type !== 'rbrace') {
        const mIdx = index;
        if (peek()?.type === 'keyword' && peek()!.value === 'default') {
          consume();
        } else {
          parseExpression(depth + 1);
        }
        
        if (peek()?.type === 'colon' || peek()?.type === 'arrow') {
          consume();
        } else {
          // If we don't see colon/arrow, and it's not the end, it's a malformed arm
          if (peek() && peek()!.type !== 'rbrace' && peek()!.type !== 'comma') {
             expect('arrow'); // Error reporting
          }
        }
        
        // Handle block or expression
        if (peek()?.type === 'lbrace') {
          parseBlock();
          armTypes.push({ kind: TypeKind.Unknown });
        } else {
          armTypes.push(parseExpression(depth + 1));
        }
        
        if (peek()?.type === 'comma') consume();
        if (index === mIdx) {
          // If perfectly stuck, skip this token to avoid infinite loop
          consume();
        }
      }
      expect('rbrace');
      
      // Basic type inference: if all arms return same kind, use it
      if (armTypes.length > 0) {
        const first = armTypes[0].kind;
        if (armTypes.every(t => t.kind === first)) return { kind: first };
      }
      return { kind: TypeKind.Unknown };
    }

    return { kind: TypeKind.Unknown };
  }

  function parseDestructuringPattern(): DestructInfo[] {
    const infos: DestructInfo[] = [];
    const startTok = peek();
    if (!startTok) return infos;

    if (startTok.type === 'lbrace') {
      consume(); // {
      while (peek() && peek()!.type !== 'rbrace') {
        if (peek()?.type === 'dot_dot_dot') {
          consume();
          const restTok = expect('identifier');
          if (restTok) infos.push({ name: restTok.value, isRest: true, isObject: true, range: restTok.range });
        } else {
          const propTok = consume();
          if (propTok && (propTok.type === 'identifier' || propTok.type === 'keyword')) {
            let propName = propTok.value;
            let alias = propName;
            let aliasRange = propTok.range;
            let propRange = propTok.range;
            if (peek()?.type === 'colon') {
              consume(); // :
              if (peek()?.type === 'lbrace' || peek()?.type === 'lbracket') {
                const nested = parseDestructuringPattern();
                infos.push({ name: alias, propName, nested, isObject: true, range: aliasRange, propRange });
                if (peek()?.type === 'comma') consume();
                continue;
              } else {
                const aliasTok = expect('identifier');
                if (aliasTok) {
                   alias = aliasTok.value;
                   aliasRange = aliasTok.range;
                }
              }
            }
            infos.push({ name: alias, propName, isObject: true, range: aliasRange, propRange });
          }
        }
        if (peek()?.type === 'comma') consume(); else break;
      }
      expect('rbrace');
    } else if (startTok.type === 'lbracket') {
      consume(); // [
      let idx = 0;
      while (peek() && peek()!.type !== 'rbracket') {
        if (peek()?.type === 'dot_dot_dot') {
          consume();
          const restTok = expect('identifier');
          if (restTok) infos.push({ name: restTok.value, isRest: true, isObject: false, range: restTok.range });
        } else {
          const itemTok = peek();
          if (itemTok?.type === 'identifier') {
            const itok = consume()!;
            infos.push({ name: itok.value, propName: idx.toString(), isObject: false, range: itok.range });
          } else if (itemTok?.type === 'lbrace' || itemTok?.type === 'lbracket') {
            const ntok = itemTok;
            const nested = parseDestructuringPattern();
            infos.push({ name: '', propName: idx.toString(), nested, isObject: false, range: ntok.range });
          }
        }
        idx++;
        if (peek()?.type === 'comma') consume(); else break;
      }
      expect('rbracket');
    }
    return infos;
  }

  function applyDestructuring(infos: DestructInfo[], source: TypeInfo | undefined, isMutable: boolean) {
    for (const info of infos) {
      if (info.isRest) {
        const type: TypeInfo = { kind: info.isObject ? TypeKind.Object : TypeKind.Array, isMutable };
        currentScope.set(info.name, type);
        if (info.range) addHover(info.range, type);
        continue;
      }
      
      let propType: TypeInfo = { kind: TypeKind.Unknown };
      if (source) {
        if (!info.isObject && source.kind === TypeKind.Array) {
          propType = source.elementType || { kind: TypeKind.Unknown };
          if (source.members) {
             const indexed = source.members.get(info.propName!);
             if (indexed) propType = indexed;
          }
        } else if (source.members && source.members.has(info.propName!)) {
          propType = source.members.get(info.propName!)!;
        } else if (!info.isObject && (info.propName === '0' || info.propName === '1')) {
          // Universal Result Pattern
          if (info.propName === '0') {
             propType = mergeTypes(source, { kind: TypeKind.Null });
          } else {
             propType = mergeTypes({ kind: TypeKind.Error, members: BUILTIN_MEMBERS.get('Error'), name: 'Error' }, { kind: TypeKind.Null });
          }
        }
      }

      // Heuristic: promote common error pattern to Error type if unknown or number
      if (!info.isObject && info.propName === '1' && (info.name === 'err' || info.name === 'error')) {
        if (propType.kind === TypeKind.Unknown || propType.kind === TypeKind.Number) {
           propType = { kind: TypeKind.Error, members: BUILTIN_MEMBERS.get('Error'), name: 'Error' };
        }
      }
      
      if (info.propRange) addHover(info.propRange, propType);

      if (info.nested) {
        if (info.range) addHover(info.range, propType);
        applyDestructuring(info.nested, propType, isMutable);
      } else if (info.name) {
        const type = { ...propType, isMutable };
        currentScope.set(info.name, type);
        if (info.range) addHover(info.range, type);
      }
    }
  }

  function parseBlock(skipBodies = false): TypeInfo {
    let lastType: TypeInfo = { kind: TypeKind.Void };
    if (peek()?.type === 'lbrace') {
      consume();
      while (peek() && peek()!.type !== 'rbrace') {
        const idx = index;
        lastType = parseStatement(skipBodies);
        if (index === idx) consume();
      }
      expect('rbrace');
    } else {
      lastType = parseStatement(skipBodies);
    }
    return lastType;
  }

  function parseStatement(skipBodies = false): TypeInfo {
    let tok = peek();
    if (!tok) return { kind: TypeKind.Unknown };

    // Iterative doc comment parsing
    while (tok && tok.type === 'doc_comment') {
      lastDoc = consume()!.value;
      tok = peek();
    }
    if (!tok) return { kind: TypeKind.Unknown };

    // export prefix
    let isExported = isSpd;
    if (tok.type === 'keyword' && tok.value === 'export') {
      consume();
      isExported = true;
      tok = peek();
      if (!tok) return { kind: TypeKind.Unknown };
    }

    if (tok.type === 'keyword' && (['set', 'var', 'define', 'class', 'layout', 'readonly', 'abstract', 'private'].includes(tok.value))) {
      while (peek() && ['readonly', 'abstract', 'private'].includes(peek()!.value)) consume();
      const next = peek();
      if (!next) return { kind: TypeKind.Unknown };

      const kw = next.value;
      if (['set', 'var', 'define', 'class', 'layout'].includes(kw)) consume();

      if (kw === 'layout') {
        const nameTok = consume();
        if (nameTok && nameTok.type === 'identifier') {
          let layoutType: SymbolInfo = { kind: TypeKind.Object, name: nameTok.value, documentation: formatDoc(lastDoc), members: new Map(), isStrict: true };
          
          if (peek()?.type === 'equals') {
            consume(); // =
            const aliased = parseTypeAnnotationTypeOnly();
            layoutType = { ...aliased, name: nameTok.value, documentation: layoutType.documentation };
          } else if (peek()?.type === 'lbrace') {
            consume(); // {
            const members = new Map<string, TypeInfo>();
            while (peek() && peek()! .type !== 'rbrace') {
              const pName = consume();
              if (pName && pName.type === 'identifier') {
                expect('colon');
                const pType = parseTypeAnnotationTypeOnly();
                members.set(pName.value, pType);
                addHover(pName.range, pType);
                if (peek()?.type === 'comma') consume();
              } else break;
            }
            expect('rbrace');
            layoutType.members = members;
          }
          currentScope.set(nameTok.value, layoutType);
          addHover(nameTok.range, layoutType);
          if (isExported) layoutType.isExported = true;
        }
        return { kind: TypeKind.Object };
      }

      if (kw === 'class') {
        const name = consume();
        if (name && name.type === 'identifier') {
          const classMembers = new Map<string, SymbolInfo>();
          const classType: SymbolInfo = { 
            kind: TypeKind.Object, 
            members: classMembers as any,
            documentation: formatDoc(lastDoc),
            name: name.value,
            isClass: true,
            isStrict: true
          };
          currentScope.set(name.value, classType);
          if (isExported) classType.isExported = true;
          addHover(name.range, classType);

          expect('lbrace');
          // --- Pass 1: Discovery ---
          const bodyStartIdx = index;
          let depth = 1;
          while (index < tokens.length && depth > 0) {
              const t = tokens[index];
              if (t.type === 'lbrace') depth++;
              else if (t.type === 'rbrace') depth--;
              else if (depth === 1 && t.type === 'keyword' && (t.value === 'var' || t.value === 'set' || t.value === 'define' || t.value === 'readonly')) {
                  const mKw = t.value;
                  const mNameTok = tokens[index + 1];
                  if (mNameTok && mNameTok.type === 'identifier') {
                      classMembers.set(mNameTok.value, { kind: TypeKind.Unknown, name: mNameTok.value, isMutable: mKw === 'var' || mKw === 'set' });
                  }
              }
              if (depth > 0) index++;
          }
          const bodyEndIdx = index;

          // --- Pass 2: Resolve Fields (Skip Methods) ---
          index = bodyStartIdx;
          const prevScope = currentScope;
          currentScope = classMembers;
          currentScope.set('this', classType);
          
          while (index < bodyEndIdx && peek() && peek()!.type !== 'rbrace') {
            const mIdx = index;
            const subTok = peek();
            if (subTok?.type === 'keyword' && (subTok.value === 'var' || subTok.value === 'set' || subTok.value === 'readonly')) {
              parseStatement(false);
            } else if (subTok?.type === 'keyword' && subTok.value === 'define') {
              parseStatement(true); // Skip Method Bodies
            } else {
              consume();
            }
            if (index === mIdx) consume();
          }

          // --- Pass 3: Resolve Methods ---
          // Reset index and Ensure 'this' is definitely in scope with the FINAL classMembers
          index = bodyStartIdx;
          currentScope = classMembers; 
          currentScope.set('this', classType);

          while (index < bodyEndIdx && peek() && peek()!.type !== 'rbrace') {
            const mIdx = index;
            const subTok = peek();
            if (subTok?.type === 'keyword' && subTok.value === 'define') {
              parseStatement(false); // Parse Method Bodies
            } else {
              consume();
            }
            if (index === mIdx) consume();
          }

          currentScope = prevScope;
          index = bodyEndIdx;
          expect('rbrace');
        }
        return { kind: TypeKind.Object };
      }

      if (peek()?.type === 'lbrace' || peek()?.type === 'lbracket') {
        const infos = parseDestructuringPattern();
        if (peek()?.type === 'colon') {
           parseTypeAnnotation();
        }
        if (peek()?.type === 'equals') consume();
        const rhs = parseExpression();
        applyDestructuring(infos, rhs, kw === 'var' || kw === 'set');
        return { kind: TypeKind.Unknown };
      }

      const nameTok = expect('identifier');
      if (nameTok) {
        let annotatedType: TypeInfo | undefined;
        if (peek()?.type === 'colon') {
           annotatedType = parseTypeAnnotation();
        }

        if (kw === 'define') {
          if (peek()?.type === 'equals') consume();
          const prevScope = currentScope;
          const existingFunc = prevScope.get(nameTok.value);
          currentScope = new Map(currentScope);
          
          let params: string[] = [];
          let parameterSymbols: SymbolInfo[] = [];
          let paramTypes: TypeInfo[] = [];
          let hasRest = false;

          const docParamTypes = parseDocParamTypes(lastDoc || '');

          if (peek()?.type === 'lparen') {
            consume();
            let pendingParams: SymbolInfo[] = [];
            let paramIdx = 0;
            while (peek() && peek()!.type !== 'rparen') {
              if (peek()?.type === 'dot_dot_dot') { consume(); hasRest = true; }
              const paramTok = expect('identifier');
              if (paramTok) {
                 const sym: SymbolInfo = { kind: TypeKind.Unknown, name: paramTok.value, isMutable: true };
                 
                 // Inherit from DocComment
                 if (docParamTypes.has(paramTok.value)) {
                    Object.assign(sym, docParamTypes.get(paramTok.value));
                 }
                 
                 // Inherit type from previous pass (discovery pass) if available
                 if (existingFunc?.parameterSymbols?.[paramIdx]) {
                     const oldSym = existingFunc.parameterSymbols[paramIdx];
                     if (oldSym.kind !== TypeKind.Unknown) {
                         sym.kind = oldSym.kind;
                         if (oldSym.members) sym.members = new Map(oldSym.members);
                     }
                 }

                 params.push(paramTok.value);
                 parameterSymbols.push(sym);
                 paramTypes.push(sym);
                 pendingParams.push(sym);
                 currentScope.set(paramTok.value, sym);
                 addHover(paramTok.range, sym);

                 if (peek()?.type === 'colon') {
                    consume(); // :
                    const result = parseTypeAnnotationTypeOnly();
                    pendingParams.forEach(p => {
                      const { name: _typeName, range: _typeRange, ...typeProps } = result;
                      Object.assign(p, typeProps);
                    });
                    pendingParams = [];
                 }
              }
              paramIdx++;
              if (peek()?.type === 'comma') consume(); else break;
            }
            expect('rparen');
          }
          
          let rType: TypeInfo = { kind: TypeKind.Unknown };
          if (peek()?.type === 'colon') {
             rType = parseTypeAnnotation();
          }

          if (peek()?.type === 'arrow' || peek()?.type === 'equals') consume();
          
          const funcType: SymbolInfo = { 
            kind: TypeKind.Function, 
            params, 
            parameterSymbols,
            paramTypes,
            hasRest,
            returnType: rType, 
            documentation: formatDoc(lastDoc) 
          };
          
          let bodyType: TypeInfo = { kind: TypeKind.Unknown };
          functionStack.push(funcType);
          const isBraced = peek()?.type === 'lbrace';
          if (skipBodies) {
             // Skip logic: match closing brace if lbrace is present
             if (isBraced) {
                let depth = 1;
                consume();
                while (index < tokens.length && depth > 0) {
                   const t = tokens[index++];
                   if (t.type === 'lbrace') depth++;
                   else if (t.type === 'rbrace') depth--;
                }
             } else {
                parseExpression(); // arrow body
             }
          } else {
             bodyType = parseBlock();
          }
          functionStack.pop();
          
          if (rType.kind === TypeKind.Unknown) {
              if (isBraced && !funcType.hasExplicitReturn) {
                  // Procedural block with no returns defaults to void
                  funcType.returnType = { kind: TypeKind.Void };
              } else {
                  funcType.returnType = mergeTypes(funcType.returnType || { kind: TypeKind.Unknown }, bodyType);
              }
          }
          
          if (lastDoc) {
            const docInfo = parseDocComment(lastDoc);
            if (docInfo.returnType) funcType.returnType = { kind: docInfo.returnType };
          }

          currentScope = prevScope;
          currentScope.set(nameTok.value, funcType);
          if (isExported) funcType.isExported = true;
          addHover(nameTok.range, funcType);
          lastDoc = undefined;
        } else {
          if (peek()?.type === 'equals') consume();
          
          const info = parseExpression(0, annotatedType);
          
          if (annotatedType && !isTypeCompatible(annotatedType, info)) {
              diagnostics.push({
                range: info.range || nameTok.range,
                message: `Type mismatch in assignment: expected ${formatType(annotatedType)}, but found ${formatType(info)}`,
                severity: DiagnosticSeverity.Error,
                data: {
                    expectedType: formatType(annotatedType),
                    foundType: formatType(info),
                    annotatedRange: annotatedType.range,
                    valueRange: info.range || nameTok.range
                }
              });
          }
          
          let finalType = annotatedType || info;
          if (annotatedType && info.kind === TypeKind.Array && annotatedType.kind === TypeKind.Array) {
              finalType = { ...info, elementType: annotatedType.elementType || info.elementType };
          }

          const symbolType: SymbolInfo = { 
            ...finalType, 
            isMutable: kw === 'var',
            value: info.value,
            documentation: finalType.documentation || info.documentation || lastDoc,
            examples: finalType.examples || info.examples || []
          };
          
          currentScope.set(nameTok.value, symbolType);
          if (isExported) symbolType.isExported = true;
          addHover(nameTok.range, symbolType);
          lastDoc = undefined;
        }
      }
      return { kind: TypeKind.Unknown };
    }

    if (tok.type === 'keyword') {
      if (tok.value === 'if') {
        consume();
        let hasParens = false;
        if (peek()?.type === 'lparen') { consume(); hasParens = true; }
        parseExpression();
        if (hasParens) expect('rparen');
        const tType = parseBlock();
        let eType: TypeInfo = { kind: TypeKind.Unknown };
        if (peek()?.type === 'keyword' && peek()!.value === 'else') {
          consume();
          if (peek()?.type === 'keyword' && peek()!.value === 'if') eType = parseStatement();
          else eType = parseBlock();
        }
        return mergeTypes(tType, eType);
      }
      if (tok.value === 'while') {
        consume();
        let hasParens = false;
        if (peek()?.type === 'lparen') { consume(); hasParens = true; }
        parseExpression();
        if (hasParens) expect('rparen');
        parseBlock();
        return { kind: TypeKind.Void };
      }
      if (tok.value === 'for') {
        consume();
        let hasParens = false;
        if (peek()?.type === 'lparen') { consume(); hasParens = true; }
        const id = consume();
        if (id?.type === 'identifier') {
          if (peek()?.type === 'keyword' && peek()!.value === 'in') consume();
          const iterable = parseExpression();
          
          let itemType: TypeInfo = { kind: TypeKind.Unknown, isMutable: true };
          if (iterable.kind === TypeKind.Array && iterable.elementType) {
            itemType = { ...iterable.elementType, isMutable: true };
          } else if (iterable.name === 'range' || (iterable.returnType?.kind === TypeKind.Array && iterable.returnType.elementType?.kind === TypeKind.Number)) {
            itemType = { kind: TypeKind.Number, isMutable: true };
          }

          // Add loop variable to scope
          currentScope.set(id.value, itemType);
          addHover(id.range, itemType);

          if (hasParens) expect('rparen');
          parseBlock();
        }
        return { kind: TypeKind.Void };
      }
      if (tok.value === 'match') {
        return parseExpression();
      }
      if (tok.value === 'return') {
        consume();
        const retExpr = (peek() && peek()!.type !== 'keyword' && peek()!.type !== 'rbrace') ? parseExpression() : { kind: TypeKind.Void };
        
        if (functionStack.length > 0) {
            const currentFn = functionStack[functionStack.length - 1] as SymbolInfo;
            currentFn.hasExplicitReturn = true;
            currentFn.returnType = mergeTypes(currentFn.returnType || { kind: TypeKind.Unknown }, retExpr);
        }
        return retExpr;
      }
    }

    if (tok.type === 'keyword' && tok.value === 'use') {
      consume();
      if (peek()?.type === 'lbrace') {
        consume();
        const imports: { name: string, alias?: string }[] = [];
        while (peek() && peek()!.type !== 'rbrace') {
          const id = expect('identifier');
          if (id) {
            let alias: string | undefined;
            if (peek()?.type === 'keyword' && peek()!.value === 'as') {
              consume();
              const aliasTok = expect('identifier');
              if (aliasTok) alias = aliasTok.value;
            }
            imports.push({ name: id.value, alias });
          }
          if (peek()?.type === 'comma') consume(); else break;
        }
        expect('rbrace');
        if (peek()?.type === 'keyword' && peek()!.value === 'from') {
          consume();
          const next = peek();
          if (next?.type === 'identifier' || next?.type === 'string') {
            const modName = consume()!.value.replace(/['"]/g, '');
            const modSymbols = resolveModule(modName);
            const isNative = modName.startsWith('native_');
            if (modSymbols || isNative) {
              const symbols = modSymbols || new Map<string, SymbolInfo>();
              imports.forEach(imp => {
                let sym = symbols.get(imp.name);
                if (!sym && isNative) {
                  sym = { kind: TypeKind.Unknown, name: imp.name, isExported: true };
                }
                if (sym && sym.isExported) {
                  currentScope.set(imp.alias || imp.name, sym);
                }
              });
            }
          }
        }
      } else {
        const modTok = expect('identifier');
        if (modTok) {
          const modName = modTok.value;
          let alias = modName;
          if (peek()?.type === 'keyword' && peek()!.value === 'as') {
            consume();
            const aliasTok = expect('identifier');
            if (aliasTok) alias = aliasTok.value;
          }
          
          const modSymbols = resolveModule(modName);
          const isNative = modName.startsWith('native_');
          if (modSymbols || isNative) {
            const exportedMembers = new Map<string, SymbolInfo>();
            if (modSymbols) {
              for (const [name, sym] of modSymbols.entries()) {
                if (sym.isExported) exportedMembers.set(name, sym);
              }
            }
            currentScope.set(alias, { kind: TypeKind.Object, members: exportedMembers, name: modName, isNative });
          }
        }
      }
      return { kind: TypeKind.Void };
    }

    const lhs = parseExpression();
    if (peek()?.type === 'equals') {
      consume();
      lastStatementWasAssignment = true;
      const rhs = parseExpression();
      
      // Assignment-based member inference (useful for classes and usage-based objects)
      if (lhs.name && lhs.receiver && lhs.receiver.members) {
          const current = lhs.receiver.members.get(lhs.name);
          // Protection: Don't clobber established types with unknown from method parameters
          if (!current || current.kind === TypeKind.Unknown) {
              lhs.receiver.members.set(lhs.name, { ...rhs, name: lhs.name });
          }
          // Local variable inference
          const sym = currentScope.get(lhs.name);
          if (sym && sym.kind === TypeKind.Unknown && rhs.kind !== TypeKind.Unknown) {
              const { range: _r, ...rhsType } = rhs;
              currentScope.set(lhs.name, { ...sym, ...rhsType, isMutable: true });
          }
      }
      
      return { kind: TypeKind.Void }; // Assignment statement returns void
    }
    return lhs;
  }
  // ─── Main Parsing Pass ───────────────────────────────────────────────────────
  let tentativeDiagnostics: Diagnostic[] = [];
  let tentativeExpansions: { sym: SymbolInfo, propName: string, propType: TypeInfo }[] = [];
  let lastStatementWasAssignment = false;

  while (index < tokens.length) {
    const idx = index;
    tentativeDiagnostics = [];
    tentativeExpansions = [];
    parseStatement();
    if (index > idx) {
      if (lastStatementWasAssignment) {
        // Commit expansions
        tentativeExpansions.forEach(exp => {
          if (!exp.sym.members) exp.sym.members = new Map<string, SymbolInfo>();
          exp.sym.members.set(exp.propName, { ...exp.propType, name: exp.propName });
        });
      } else {
        // Commit diagnostics
        diagnostics.push(...tentativeDiagnostics);
      }
      lastStatementWasAssignment = false;
    } else {
      const skipTok = consume();
      if (skipTok && skipTok.type !== 'doc_comment') {
        diagnostics.push({
          range: skipTok.range,
          message: `Unexpected token outside of valid statement: '${skipTok.value}'`,
          severity: DiagnosticSeverity.Error
        });
      }
    }
  }

  return { diagnostics, symbols, tokens, importedModules, hoverMap };
}

function resolveMemberInfo(left: TypeInfo, propName: string): TypeInfo | null {
  // If we have a name and IT matches a builtin specialized type, use THAT even if kind is Object
  if (left.name && BUILTIN_MEMBERS.has(left.name) && left.name !== 'Object') {
    const bMembers = BUILTIN_MEMBERS.get(left.name)!;
    if (bMembers.has(propName)) return bMembers.get(propName)!;
  }
  
  // Implicit Future unwrapping: if left is Future and we are not accessing wait/error, unwrap
  if (left.name === 'Future' && propName !== 'wait' && propName !== 'error') {
      if (left.returnType) {
          const unwrapped = resolveMemberInfo(left.returnType, propName);
          if (unwrapped) return unwrapped;
      }
      // Fallback: search Object members if unwrapping Future
      const objMembers = BUILTIN_MEMBERS.get('Object');
      if (objMembers?.has(propName)) return objMembers.get(propName)!;
  }

  // 1. Explicit members (objects/classes/JSON/Layouts)
  if (left.members?.has(propName)) return left.members.get(propName)!;

  // 2. Builtin Instance Members (Date, BigInt, String, Array, etc.)
  let builtinName: string | undefined = undefined;
  if (left.name === 'Response') builtinName = 'Response';
  else if (left.name === 'Request') builtinName = 'Request';
  else if (left.name === 'Future') builtinName = 'Future';
  else if (left.name === 'Timer') builtinName = 'Timer';
  else if (left.kind === TypeKind.Date) builtinName = 'Date';
  else if (left.kind === TypeKind.BigInt) builtinName = 'BigInt';
  else if (left.kind === TypeKind.Map) builtinName = 'Map';
  else if (left.kind === TypeKind.String) builtinName = 'string';
  else if (left.kind === TypeKind.Array) builtinName = 'Array';
  else if (left.kind === TypeKind.Number) builtinName = 'number';
  else if (left.kind === TypeKind.Regex) builtinName = 'regex';
  else if (left.kind === TypeKind.Error) builtinName = 'Error';
  else if (left.kind === TypeKind.Object) builtinName = 'Object';

  if (builtinName && BUILTIN_MEMBERS.has(builtinName)) {
    const bMembers = BUILTIN_MEMBERS.get(builtinName)!;
    if (bMembers.has(propName)) return bMembers.get(propName)!;
    // If it's a strictly defined builtin type and NOT 'Object' or 'Map', it's a hard fail
    if (builtinName !== 'Object' && builtinName !== 'Map' && builtinName !== 'Array') {
       return null; 
    }
  }

  // 3. Special Properties
  if ((left.kind === TypeKind.Array || left.kind === TypeKind.String) && propName === 'length') {
    return { kind: TypeKind.Number };
  }

  // 4. Return type propagation (chained calls)
  if (left.returnType && left.returnType.members?.has(propName)) {
    return left.returnType.members.get(propName)!;
  }

  // 5. Fallback for Arrays/Maps/Objects without explicit member
  if (left.kind === TypeKind.Array && left.elementType) return left.elementType;
  
  if (left.kind === TypeKind.Map || left.kind === TypeKind.Object || left.kind === TypeKind.Array || left.kind === TypeKind.Unknown) {
    if (left.isStrict || (left.name && BUILTIN_MEMBERS.has(left.name) && left.name !== 'Object')) return null;
    return { kind: TypeKind.Unknown };
  }

  if (left.isNative) {
    return { kind: TypeKind.Unknown, name: propName };
  }

  return null;
}

// ─── Crash Protection ─────────────────────────────────────────────────────────

process.on('uncaughtException', (err) => {
  connection.console.error(`[SP] Uncaught exception: ${err.message}\n${err.stack}`);
});

process.on('unhandledRejection', (reason) => {
  connection.console.error(`[SP] Unhandled rejection: ${reason}`);
});

function validateTextDocument(textDocument: TextDocument): void {
  const text = textDocument.getText();
  const uri = textDocument.uri;
  const parsed = parseAndType(text, uri);
  
  connection.sendDiagnostics({ uri, diagnostics: parsed.diagnostics });
}

let validationTimer: NodeJS.Timeout | undefined;
function debounceValidate(textDocument: TextDocument) {
  if (validationTimer) clearTimeout(validationTimer);
  validationTimer = setTimeout(() => {
    validateTextDocument(textDocument);
    validationTimer = undefined;
  }, 200);
}

documents.onDidChangeContent(change => { debounceValidate(change.document); });
documents.onDidOpen(change => { validateTextDocument(change.document); });

documents.listen(connection);
connection.listen();
