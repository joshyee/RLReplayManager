/*
** RLReplayManager
**
** Copyright (C) 2015 Tobias Taschner <github@tc84.de>
**
** Licensed under GPL v3 or later
*/

#include "TransferManager.h"

#include <wx/config.h>
#include <wx/wfstream.h>
#include <wx/sstream.h>
#include <wx/time.h>
#include <wx/filename.h>

#include <sstream>
#include "curl_easy.h"
#include "curl_form.h"
#include "curl_header.h"

#include "wx/jsonreader.h"

using curl::curl_easy;
using curl::curl_form;
using curl::curl_header;

wxDEFINE_EVENT(wxEVT_TRANSFER_UPDATE, wxCommandEvent);

class WebRequest
{
public:
	WebRequest(const wxString& url):
		m_url(url),
		m_writer(m_responseStream),
		m_easy(m_writer)
	{

	}

	wxJSONValue GetResponse(bool useAuth = true)
	{
		try
		{
			m_easy.add(curl_pair<CURLoption, string>(CURLOPT_URL, m_url.ToStdString()));
			m_easy.add(curl_pair<CURLoption, long>(CURLOPT_FOLLOWLOCATION, 1L));

			curl_header hdr;
			if (useAuth)
			{
				hdr.add("Authorization: Token " + wxConfig::Get()->Read("UploadKey").ToStdString());

				m_easy.add(curl_pair<CURLoption, curl_header>(CURLOPT_HTTPHEADER, hdr));
			}

			m_easy.perform();
		}
		catch (curl_easy_exception error)
		{
			vector<pair<string, string>> errors = error.get_traceback();

			for (auto err = errors.begin(); err != errors.end(); ++err)
				wxLogError("%s\n(%s)", err->first.c_str(), err->second.c_str());

			return wxJSONValue();
		}

		wxJSONValue val;
		wxJSONReader jsonReader;
		wxLogDebug("Server: %s", m_responseStream.str().c_str());
		jsonReader.Parse(m_responseStream.str(), &val);
		return val;
	}

	curl_easy& GetEasy()
	{
		return m_easy;
	}

private:
	wxString m_url;
	std::stringstream m_responseStream;
	curl_writer m_writer;
	curl_easy m_easy;
};

//
// TransferManager
//

TransferManager* TransferManager::ms_transferManager = NULL;

TransferManager& TransferManager::Get()
{
	if (ms_transferManager == NULL)
		ms_transferManager = new TransferManager();

	return *ms_transferManager;
}

TransferManager::TransferManager()
{

}

void TransferManager::Upload(Replay::Ptr replay)
{
	m_uploadQueue.push(replay);

	wxLogDebug("%d entries in upload queue", m_uploadQueue.size());

	if (GetThread() == NULL ||
		!GetThread()->IsRunning())
	{
		if (CreateThread(wxTHREAD_JOINABLE) != wxTHREAD_NO_ERROR)
		{
			wxLogError("Could not create the worker thread!");
			return;
		}

		if (GetThread()->Run() != wxTHREAD_NO_ERROR)
		{
			wxLogError("Could not run the worker thread!");
			return;
		}
	}
}

void* TransferManager::Entry()
{
	while (!m_uploadQueue.empty())
	{
		Replay::Ptr replay = m_uploadQueue.front();
		m_uploadQueue.pop();

		wxFileName fn(replay->GetFileName());

		std::string filename = fn.GetFullPath().ToStdString();

		WebRequest req(GetAPIURL("replays/"));

		std::string fieldName = "file";
		curl_form form;
		curl_pair<CURLformoption, string> file_form(CURLFORM_COPYNAME, fieldName);
		curl_pair<CURLformoption, string> file_cont(CURLFORM_FILE, filename);

		// Form adding
		form.add(file_form, file_cont);

		long verifyHost = 0;
		req.GetEasy().add(curl_pair<CURLoption, curl_form>(CURLOPT_HTTPPOST, form));
		req.GetEasy().add(curl_pair<CURLoption, long>(CURLOPT_SSL_VERIFYPEER, verifyHost));
		

		CallAfter(&TransferManager::UpdateStatus, wxString::Format(_("Uploading %s..."), replay->GetDescription()));

		wxJSONValue resp = req.GetResponse();

		// TODO: handle response (error message, share url, etc.)

		CallAfter(&TransferManager::UpdateStatus, wxString::Format(_("Uploaded %s"), replay->GetDescription()));
	}

	return (void*)0;
}

wxString TransferManager::GetAPIURL(const wxString& method)
{
	return wxConfig::Get()->Read("APIURL", "https://www.rocketleaguereplays.com/api/") + method;
}

void TransferManager::UpdateStatus(const wxString& message)
{
	wxLogDebug("Transfer update: %s", message);
	wxCommandEvent evt(wxEVT_TRANSFER_UPDATE);
	evt.SetString(message);
	evt.SetEventObject(this);
	ProcessEvent(evt);
}
