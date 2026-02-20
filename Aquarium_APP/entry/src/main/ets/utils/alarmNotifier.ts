import notificationManager from '@ohos.notificationManager';

export async function publishAlarmNotification(params: {
  title: string;
  text: string;
  id?: number;
}): Promise<void> {
  const nm: any = notificationManager as any;
  const contentType =
    nm?.ContentType?.NOTIFICATION_CONTENT_BASIC_TEXT ??
    nm?.ContentType?.NOTIFICATION_CONTENT_NORMAL ??
    0;

  const request: any = {
    id: params.id ?? (Date.now() % 2147483647),
    content: {
      contentType,
      normal: {
        title: params.title,
        text: params.text,
      },
    },
  };

  const ret = nm?.publish?.(request);
  if (ret && typeof ret.then === 'function') {
    await ret;
    return;
  }

  await new Promise<void>((resolve, reject) => {
    nm?.publish?.(request, (err: any) => {
      if (err) {
        reject(err);
      } else {
        resolve();
      }
    });
  });
}

