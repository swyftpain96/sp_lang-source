import * as path from 'path';
import { workspace, ExtensionContext } from 'vscode';
import {
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
  TransportKind
} from 'vscode-languageclient/node';

let client: LanguageClient | undefined;

export function activate(context: ExtensionContext) {
  const serverModule = context.asAbsolutePath(path.join('out', 'server', 'src', 'server.js'));

  const serverOptions: ServerOptions = {
    run: { module: serverModule, transport: TransportKind.ipc },
    debug: {
      module: serverModule,
      transport: TransportKind.ipc,
      options: {
        execArgv: ['--nolazy', '--inspect=6009']
      }
    }
  };

  const clientOptions: LanguageClientOptions = {
    documentSelector: [{ scheme: 'file', language: 'sp' }],
    synchronize: {
      fileEvents: workspace.createFileSystemWatcher('**/*.sp')
    }
  };

  client = new LanguageClient(
    'spLanguageServer',
    'SP Language Server',
    serverOptions,
    clientOptions
  );

  // Start the client
  client.start();

  // Register it for disposal
  context.subscriptions.push(client);
}

export function deactivate(): Thenable<void> | undefined {
  if (!client) {
    return undefined;
  }
  return client.stop();
}
