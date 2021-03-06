//---------------------------------------------------------------------------

#ifndef ProgrammableButtonH
#define ProgrammableButtonH
//---------------------------------------------------------------------------
#include <SysUtils.hpp>
#include <Classes.hpp>
#include <Controls.hpp>
#include <ExtCtrls.hpp>
//---------------------------------------------------------------------------

#include "baresip_dialog_info_status.h"
#include "baresip_dialog_info_direction.h"
#include "baresip_presence_status.h"

class ButtonConf;

class PACKAGE TProgrammableButton : public TPanel
{
private:
	TLabel *label;
	TLabel *label2;
	TImage *image;
	TImageList *imgList;
	AnsiString description;
	AnsiString user;
	enum dialog_info_status state;
	enum presence_status presence_state;
	bool down;
	int scalingPercentage;
	bool once;

	int configuredLines;
	AnsiString caption2;

	bool raised;	// helps with flickering
	void Lower(void);
	void Raise(void);

	Graphics::TBitmap *bmpIdle;
	Graphics::TBitmap *bmpTerminated;
	Graphics::TBitmap *bmpEarly;
	Graphics::TBitmap *bmpConfirmed;

	void SetLines(int cnt);
	void SetImage(Graphics::TBitmap *bmp);
protected:
	void __fastcall MouseEnter(TObject *Sender);
	void __fastcall MouseLeave(TObject *Sender);
	void __fastcall MouseUpHandler(TObject *Sender, TMouseButton Button, TShiftState Shift, int X, int Y);
	void __fastcall MouseDownHandler(TObject *Sender, TMouseButton Button, TShiftState Shift, int X, int Y);
public:
	__fastcall TProgrammableButton(TComponent* Owner, TImageList* imgList, int scalingPercentage);
	__fastcall ~TProgrammableButton();
	void SetConfig(const ButtonConf &cfg);
	void SetCaption(AnsiString text);
	void SetState(enum dialog_info_status state, enum dialog_info_direction direction, AnsiString remoteIdentity, AnsiString remoteIdentityDisplay);
	enum dialog_info_status GetState(void) {
		return state;
	}
	void SetDown(bool state);
	bool GetDown(void)
	{
    	return down;
	}
	void SetImage(AnsiString file);
	void SetMwiState(int newMsg, int oldMsg);
	void SetPresenceState(enum presence_status state, AnsiString note);
	void ClearPresenceState(void);
	void UpdateCallbacks(void);
	void SetScaling(int percentage) {
		if (scalingPercentage != percentage)
		{
			scalingPercentage = percentage;
		}
    }
__published:
};
//---------------------------------------------------------------------------
#endif
