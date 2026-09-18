from livekit import api
import os
def generate_token():
    token = (api.AccessToken()
             .with_identity("ChatBot")
             .with_name("ChatBot")
             .with_grants(api.VideoGrants(
                 room_join=True,
                 room="zq_room",
                 can_publish=True,
                 can_subscribe=True
             ))
             .to_jwt())
    return token

print(generate_token())