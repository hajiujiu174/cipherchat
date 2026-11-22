# account
target_sources(messager_lib PRIVATE
    src/features/account/loginpreferences.h
    src/features/account/loginpreferences.cpp
    src/features/account/accountdialog.h
    src/features/account/accountdialog.cpp
    src/features/account/emailLoginPage.cpp
    src/features/account/logindialog.cpp)

# calls
target_sources(messager_lib PRIVATE
    src/features/calls/callmanager.h)

# chat
target_sources(messager_lib PRIVATE
    src/features/chat/conversationmodel.h
    src/features/chat/conversationmodel.cpp)

# files
target_sources(messager_lib PRIVATE
    src/features/files/localfiles.h
    src/features/files/localfiles.cpp
    src/features/files/imageimport.h
    src/features/files/imageimport.cpp)

# games
target_sources(messager_lib PRIVATE
    src/features/games/casualgames.h
    src/features/games/casualgames.cpp
    src/features/games/casualgamepage.h
    src/features/games/casualgamepage.cpp
    src/features/games/tankengine.h
    src/features/games/tankengine.cpp
    src/features/games/tankcontroller.h
    src/features/games/tankcontroller.cpp
    src/features/games/tankpage.h
    src/features/games/tankpage.cpp
    src/features/games/badmintonengine.h
    src/features/games/badmintonengine.cpp
    src/features/games/badmintoncontroller.h
    src/features/games/badmintoncontroller.cpp
    src/features/games/badmintonpage.h
    src/features/games/badmintonpage.cpp
    src/features/games/chessrules.h
    src/features/games/chessrules.cpp
    src/features/games/chesscontroller.h
    src/features/games/chesscontroller.cpp
    src/features/games/chesspage.h
    src/features/games/chesspage.cpp
    third_party/tanktrouble/upstream/game/Maze.cc
    third_party/tanktrouble/upstream/game/Object.cc
    third_party/tanktrouble/upstream/game/Tank.cc
    third_party/tanktrouble/upstream/game/Shell.cc
    third_party/tanktrouble/upstream/game/Block.cc
    third_party/tanktrouble/upstream/game/util/Math.cc
    third_party/tanktrouble/upstream/game/util/Vec.cc
    src/features/games/gomokuwire.h
    src/features/games/gomokucontroller.h
    src/features/games/gomokucontroller.cpp
    src/features/games/gomokupage.h
    src/features/games/gomokupage.cpp)

# groups
target_sources(messager_lib PRIVATE
    src/features/groups/groupmemberpicker.h
    src/features/groups/groupmemberpicker.cpp)

# miniapps
target_sources(messager_lib PRIVATE
    src/features/miniapps/miniappspage.h
    src/features/miniapps/miniappspage.cpp
    src/features/miniapps/scientificcalculator.h
    src/features/miniapps/scientificcalculator.cpp)

# social
target_sources(messager_lib PRIVATE
    src/features/social/profilepage.h
    src/features/social/profilepage.cpp
    src/features/social/momentspage.h
    src/features/social/momentspage.cpp)
