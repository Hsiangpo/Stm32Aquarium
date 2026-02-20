export interface PendingWantParams {
  baseUrl?: string;
  projectId?: string;
  deviceId?: string;
}

export class GlobalContext {
  private static appContext: object | undefined;
  private static pendingWantParams: PendingWantParams | undefined;

  static setAppContext(ctx: object): void {
    GlobalContext.appContext = ctx;
  }

  static getAppContext(): object {
    if (!GlobalContext.appContext) {
      throw new Error('AppContext is not initialized');
    }
    return GlobalContext.appContext;
  }

  static setPendingWantParams(params: PendingWantParams | undefined): void {
    GlobalContext.pendingWantParams = params;
  }

  static consumePendingWantParams(): PendingWantParams | undefined {
    const p = GlobalContext.pendingWantParams;
    GlobalContext.pendingWantParams = undefined;
    return p;
  }
}
