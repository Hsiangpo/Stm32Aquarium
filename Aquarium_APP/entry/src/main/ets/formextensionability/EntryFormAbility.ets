import FormExtensionAbility from '@ohos.app.form.FormExtensionAbility';
import formBindingData from '@ohos.app.form.formBindingData';
import formProvider from '@ohos.app.form.formProvider';
import Want from '@ohos.app.ability.Want';
import { loadCardSnapshot } from '../utils/cardSnapshotStore';

interface PlaceholderBindingData {
  empty: boolean;
}

function buildBindingData(snapshotText: Object): formBindingData.FormBindingData {
  return formBindingData.createFormBindingData(snapshotText);
}

export default class EntryFormAbility extends FormExtensionAbility {
  onAddForm(want: Want): formBindingData.FormBindingData {
    // FormExtensionAbility.onAddForm is sync; return a placeholder immediately.
    // The scheduled onUpdateForm will refresh the real snapshot.
    const placeholder: PlaceholderBindingData = { empty: true };
    return buildBindingData(placeholder);
  }

  onUpdateForm(formId: string): void {
    loadCardSnapshot()
      .then((snapshot) => {
        const placeholder: PlaceholderBindingData = { empty: true };
        const data: Object = snapshot ? snapshot : placeholder;
        const binding = buildBindingData(data);
        // Promise-style API is available in API 17.
        return formProvider.updateForm(formId, binding);
      })
      .catch(() => {
        // ignore
      });
  }
}
